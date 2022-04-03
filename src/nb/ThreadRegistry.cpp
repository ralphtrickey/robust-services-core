//==============================================================================
//
//  ThreadRegistry.cpp
//
//  Copyright (C) 2013-2022  Greg Utas
//
//  This file is part of the Robust Services Core (RSC).
//
//  RSC is free software: you can redistribute it and/or modify it under the
//  terms of the GNU General Public License as published by the Free Software
//  Foundation, either version 3 of the License, or (at your option) any later
//  version.
//
//  RSC is distributed in the hope that it will be useful, but WITHOUT ANY
//  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
//  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
//  details.
//
//  You should have received a copy of the GNU General Public License along
//  with RSC.  If not, see <http://www.gnu.org/licenses/>.
//
#include "ThreadRegistry.h"
#include "StatisticsGroup.h"
#include <algorithm>
#include <bitset>
#include <iterator>
#include <ostream>
#include <string>
#include "Debug.h"
#include "Formatters.h"
#include "MutexGuard.h"
#include "NbCliParms.h"
#include "Restart.h"
#include "Singleton.h"
#include "SysMutex.h"
#include "SysThread.h"
#include "ThisThread.h"
#include "Thread.h"
#include "ThreadAdmin.h"

using std::ostream;
using std::string;

//------------------------------------------------------------------------------

namespace NodeBase
{
static bool IsSortedByThreadId(const Thread* thr1, const Thread* thr2)
{
   return (thr1->Tid() < thr2->Tid());
}

//==============================================================================

ThreadInfo::ThreadInfo
   (ThreadId tid, ThreadState state, SysThread* systhrd, Thread* thread) :
   tid_(tid),
   state_(state),
   systhrd_(systhrd),
   thread_(thread)
{
}

//==============================================================================

class ThreadStatsGroup : public StatisticsGroup
{
public:
   ThreadStatsGroup();
   ~ThreadStatsGroup();
   void DisplayStats
      (ostream& stream, id_t id, const Flags& options) const override;
};

//------------------------------------------------------------------------------

ThreadStatsGroup::ThreadStatsGroup() : StatisticsGroup("Threads [ThreadId]")
{
   Debug::ft("ThreadStatsGroup.ctor");
}

//------------------------------------------------------------------------------

ThreadStatsGroup::~ThreadStatsGroup()
{
   Debug::ftnt("ThreadStatsGroup.dtor");
}

//------------------------------------------------------------------------------

void ThreadStatsGroup::DisplayStats
   (ostream& stream, id_t id, const Flags& options) const
{
   Debug::ft("ThreadStatsGroup.DisplayStats");

   StatisticsGroup::DisplayStats(stream, id, options);

   auto reg = Singleton< ThreadRegistry >::Instance();

   if(id == 0)
   {
      auto threads = reg->GetThreads();

      for(auto t = threads.cbegin(); t != threads.cend(); ++t)
      {
         (*t)->DisplayStats(stream, options);
      }
   }
   else
   {
      auto t = reg->GetThread(id);

      if(t == nullptr)
      {
         stream << spaces(2) << NoThreadExpl << CRLF;
         return;
      }

      t->DisplayStats(stream, options);
   }
}

//==============================================================================
//
//  Critical section lock for the thread registry.
//
static SysMutex ThreadsLock_("ThreadsLock");

//  The thread at which to start searching for the thread to be
//  scheduled in.  Scheduling is currently round-robin but will
//  eventually be changed to support proportional scheduling.
//
static SysThreadId NextSysThreadId_ = 0;

//------------------------------------------------------------------------------

ThreadRegistry::ThreadRegistry() : nextTid_(1)
{
   Debug::ft("ThreadRegistry.ctor");

   statsGroup_.reset(new ThreadStatsGroup);
}

//------------------------------------------------------------------------------

fn_name ThreadRegistry_dtor = "ThreadRegistry.dtor";

ThreadRegistry::~ThreadRegistry()
{
   Debug::ftnt(ThreadRegistry_dtor);

   Debug::SwLog(ThreadRegistry_dtor, UnexpectedInvocation, 0);
}

//------------------------------------------------------------------------------

void ThreadRegistry::ClaimBlocks()
{
   Debug::ft("ThreadRegistry.ClaimBlocks");

   for(auto t = threads_.cbegin(); t != threads_.cend(); ++t)
   {
      auto thr = t->second.thread_;
      if(thr != nullptr) thr->ClaimBlocks();
   }
}

//------------------------------------------------------------------------------

fn_name ThreadRegistry_Created = "ThreadRegistry.Created";

void ThreadRegistry::Created(SysThread* systhrd, Thread* thread)
{
   Debug::ft(ThreadRegistry_Created);

   MutexGuard guard(&ThreadsLock_);

   auto nid = systhrd->Nid();
   auto entry = threads_.find(nid);

   if(entry == threads_.cend())
   {
      auto tid = SetThreadId(thread);
      threads_.insert
         (Entry(nid, ThreadInfo(tid, Constructing, systhrd, thread)));
      return;
   }

   Debug::SwLog(ThreadRegistry_Created, "thread already exists", nid);
}

//------------------------------------------------------------------------------

fn_name ThreadRegistry_Destroying = "ThreadRegistry.Destroying";

void ThreadRegistry::Destroying(ThreadState state, const SysThread* systhrd)
{
   Debug::ft(ThreadRegistry_Destroying);

   MutexGuard guard(&ThreadsLock_);

   //  If a thread is deleted by code running on another thread, its
   //  own native thread identifier must be used to find it.
   //
   auto nid = (systhrd != nullptr ?
      systhrd->Nid() : SysThread::RunningThreadId());
   auto entry = threads_.find(nid);

   if(entry != threads_.cend())
   {
      entry->second.state_ = state;
      if(state == Deleted) entry->second.thread_ = nullptr;
      return;
   }

   Debug::SwLog(ThreadRegistry_Destroying, "thread not found", nid);
}

//------------------------------------------------------------------------------

void ThreadRegistry::Display(ostream& stream,
   const string& prefix, const Flags& options) const
{
   Permanent::Display(stream, prefix, options);

   stream << prefix << "statsGroup : ";
   stream << strObj(statsGroup_.get()) << CRLF;
   stream << prefix << "nextTid    : " << nextTid_ << CRLF;

   stream << prefix << "threads [ThreadId]" << CRLF;
   auto threads = GetThreads();

   auto lead1 = prefix + spaces(2);
   auto lead2 = prefix + spaces(4);

   for(auto t = threads.cbegin(); t != threads.cend(); ++t)
   {
      stream << lead1 << strIndex((*t)->Tid());

      if(options.test(DispVerbose))
      {
         stream << CRLF;
         (*t)->Display(stream, lead2, NoFlags);
         ThisThread::PauseOver(90);
      }
      else
      {
         stream << strObj(*t) << CRLF;
      }
   }
}

//------------------------------------------------------------------------------

void ThreadRegistry::EraseThreadId(ThreadId tid)
{
   Debug::ft("ThreadRegistry.EraseThreadId");

   //  When a thread exits, its entry is not erased.  It enters the Deleted
   //  state and remains registered against its native identifier, with its
   //  ThreadId preserved.  This allows it to be distinguished from other
   //  threads in trace tool output, even after it has exited.  ThreadIds
   //  are assigned in ascending order, as tracked by nextTid_.  When it
   //  reaches the maximum ThreadId, it wraps around and assigns ThreadIds
   //  starting at 1 again.  At this point, the ThreadId of a thread that
   //  has exited can be reassigned, so its entry must finally be erased.
   //
   for(auto t = threads_.cbegin(); t != threads_.cend(); ++t)
   {
      if(t->second.tid_ == tid)
      {
         threads_.erase(t);
         return;
      }
   }
}

//------------------------------------------------------------------------------

void ThreadRegistry::Exiting(SysThreadId nid)
{
   Debug::ft("ThreadRegistry.Exiting");

   MutexGuard guard(&ThreadsLock_);

   auto entry = threads_.find(nid);

   if(entry != threads_.cend())
   {
      entry->second.state_ = Deleted;
      entry->second.systhrd_ = nullptr;
      entry->second.thread_ = nullptr;
   }
}

//------------------------------------------------------------------------------

Thread* ThreadRegistry::FindThread(SysThreadId nid) const NO_FT
{
   auto entry = threads_.find(nid);
   if(entry == threads_.end()) return nullptr;
   return entry->second.thread_;
}

//------------------------------------------------------------------------------

ThreadId ThreadRegistry::FindTid(SysThreadId nid) const NO_FT
{
   auto entry = threads_.find(nid);
   if(entry == threads_.end()) return NIL_ID;
   return entry->second.tid_;
}

//------------------------------------------------------------------------------

fn_name ThreadRegistry_GetState = "ThreadRegistry.GetState";

ThreadState ThreadRegistry::GetState()
{
   Debug::ft(ThreadRegistry_GetState);

   MutexGuard guard(&ThreadsLock_);

   auto nid = SysThread::RunningThreadId();
   auto entry = threads_.find(nid);

   if(entry != threads_.cend())
   {
      auto state = entry->second.state_;

      if(state == Deleted)
      {
         ThreadAdmin::Incr(ThreadAdmin::Orphans);
         delete entry->second.systhrd_;
         entry->second.systhrd_ = nullptr;
         Debug::SwLog(ThreadRegistry_GetState, "orphan exited", nid);
      }

      return state;
   }

   return NotRegistered;
}

//------------------------------------------------------------------------------

Thread* ThreadRegistry::GetThread(ThreadId tid) const
{
   for(auto t = threads_.cbegin(); t != threads_.cend(); ++t)
   {
      auto thread = t->second.thread_;
      if((thread != nullptr) && (thread->Tid() == tid)) return thread;
   }

   return nullptr;
}

//------------------------------------------------------------------------------

std::vector< Thread* > ThreadRegistry::GetThreads() const
{
   std::vector< Thread* > threads;

   MutexGuard guard(&ThreadsLock_);

   for(auto t = threads_.cbegin(); t != threads_.cend(); ++t)
   {
      auto thread = t->second.thread_;
      if(thread != nullptr) threads.push_back(thread);
   }

   guard.Release();

   std::sort(threads.begin(), threads.end(), IsSortedByThreadId);
   return threads;
}

//------------------------------------------------------------------------------

fn_name ThreadRegistry_Initialized = "ThreadRegistry.Initialized";

void ThreadRegistry::Initialized(SysThreadId nid)
{
   Debug::ft(ThreadRegistry_Initialized);

   MutexGuard guard(&ThreadsLock_);

   auto entry = threads_.find(nid);

   if(entry != threads_.cend())
      entry->second.state_ = Constructed;
   else
      Debug::SwLog(ThreadRegistry_Initialized, "thread not found", nid);
}

//------------------------------------------------------------------------------

bool ThreadRegistry::IsDeleted() const
{
   MutexGuard guard(&ThreadsLock_);

   auto nid = SysThread::RunningThreadId();
   auto entry = threads_.find(nid);

   if(entry != threads_.cend())
   {
      return (entry->second.state_ == Deleted);
   }

   return false;
}

//------------------------------------------------------------------------------

void ThreadRegistry::Patch(sel_t selector, void* arguments)
{
   Permanent::Patch(selector, arguments);
}

//------------------------------------------------------------------------------

std::set< Thread* > ThreadRegistry::Restarting(RestartLevel level) const
{
   Debug::ft("ThreadRegistry.Restarting");

   std::set< Thread* > exiting;

   auto threads = GetThreads();

   for(auto t = threads.rbegin(); t != threads.rend(); ++t)
   {
      if((*t)->Restarting(level))
      {
         exiting.insert(*t);
      }
   }

   return exiting;
}

//------------------------------------------------------------------------------

Thread* ThreadRegistry::Select() const
{
   Debug::ft("ThreadRegistry.Select");

   //  Cycle through all threads, beginning with the one identified by
   //  NextSysThreadId_, to find the next one that can be scheduled.
   //
   auto t = threads_.find(NextSysThreadId_);
   if(t == threads_.end()) t = threads_.begin();
   Thread* next = nullptr;

   for(NO_OP; t != threads_.end(); ++t)
   {
      auto thread = t->second.thread_;
      if(thread == nullptr) continue;

      if(thread->CanBeScheduled())
      {
         next = thread;
         break;
      }
   }

   if(next == nullptr)
   {
      for(t = threads_.begin(); t != threads_.end(); ++t)
      {
         auto thread = t->second.thread_;
         if(thread == nullptr) continue;

         if(thread->CanBeScheduled())
         {
            next = thread;
            break;
         }
      }
   }

   //  If a thread was found, start the next search with the thread
   //  that follows it.
   //
   if(next != nullptr)
   {
      auto entry = threads_.find(next->NativeThreadId());

      while(true)
      {
         ++entry;

         if(entry == threads_.end())
         {
            NextSysThreadId_ = 0;
            break;
         }

         auto thread = entry->second.thread_;
         if(thread == nullptr) continue;

         NextSysThreadId_ = thread->NativeThreadId();
         break;
      }
   }

   return next;
}

//------------------------------------------------------------------------------

ThreadId ThreadRegistry::SetThreadId(Thread* thread)
{
   Debug::ft("ThreadRegistry.SetThreadId");

   //  Get a list of all threads, sorted by ThreadId.  Assign the first
   //  available identifier, starting at nextTid_, to THREAD.
   //
   ThreadId tid = nextTid_++;
   if(nextTid_ > Thread::MaxId) nextTid_ = 1;

   auto threads = GetThreads();

   for(auto t = threads.cbegin(); t != threads.cend(); ++t)
   {
      auto curr = (*t)->Tid();

      if(curr < tid)
         continue;
      else if(curr > tid)
         break;
      else
         ++tid;
   }

   EraseThreadId(tid);
   thread->SetTid(tid);
   return tid;
}

//------------------------------------------------------------------------------

void ThreadRegistry::Shutdown(RestartLevel level)
{
   Debug::ft("ThreadRegistry.Shutdown");

   auto threads = GetThreads();

   for(auto t = threads.rbegin(); t != threads.rend(); ++t)
   {
      (*t)->Shutdown(level);
   }

   Restart::Release(statsGroup_);
}

//------------------------------------------------------------------------------

size_t ThreadRegistry::Size()
{
   auto reg = Singleton< ThreadRegistry >::Extant();
   if(reg == nullptr) return 0;
   return reg->Threads().size();
}

//------------------------------------------------------------------------------

void ThreadRegistry::Startup(RestartLevel level)
{
   Debug::ft("ThreadRegistry.Startup");

   //  This starts up all threads that survived the restart.
   //
   nextTid_ = 1;
   if(statsGroup_ == nullptr) statsGroup_.reset(new ThreadStatsGroup);

   auto threads = GetThreads();

   for(auto t = threads.begin(); t != threads.end(); ++t)
   {
      (*t)->Startup(level);
   }
}

//------------------------------------------------------------------------------

void ThreadRegistry::TrimThreads(std::set< Thread* >& threads) const
{
   Debug::ft("ThreadRegistry.TrimThreads");

   auto extant = GetThreads();

   for(auto t = threads.begin(); t != threads.end(); NO_OP)
   {
      if(std::find(extant.cbegin(), extant.cend(), *t) == extant.cend())
         t = threads.erase(t);
      else
         ++t;
   }
}
}