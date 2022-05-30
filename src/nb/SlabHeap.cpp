//==============================================================================
//
//  SlabHeap.cpp
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
#include "SlabHeap.h"
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include "Debug.h"
#include "Duration.h"
#include "Element.h"
#include "Formatters.h"
#include "NbTypes.h"
#include "Restart.h"
#include "SysMemory.h"
#include "SysMutex.h"

using std::ostream;
using std::string;

//------------------------------------------------------------------------------

namespace NodeBase
{
//  For tracking a memory segment allocated by the heap.
//
struct SlabInfo
{
   SlabInfo(void* addr, size_t size) : addr_(addr), size_(size) { }

   void Print(ostream& stream) const
   {
      stream << "addr=" << addr_ << spaces(2);
      stream << "size=" << size_;
   }

   void* const addr_;   // segment's address
   const size_t size_;  // segment's size
};

//  A slab identifier.
//
typedef uint16_t SlabId;

//> The maximum slab identifier.
//
constexpr SlabId MaxSlab = 1023;

//------------------------------------------------------------------------------
//
//  An area's state.
//
enum AreaState : uint8_t
{
   FREE,
   USED
};

ostream& operator<<(ostream& stream, AreaState state)
{
   switch(state)
   {
   case FREE:
      stream << "free";
      break;
   case USED:
      stream << "used";
      break;
   default:
      stream << ERROR_STR;
   }

   return stream;
}

//------------------------------------------------------------------------------
//
//  For tracking an area within a slab.  An area is a portion of the slab
//  that was created to satisfy an allocation request.
//
struct AreaInfo
{
   AreaInfo(void* addr, size_t size, SlabId slab, AreaState state) :
      addr_(addr), size_(size), slab_(slab), state_(state) { }

   void Print(ostream& stream) const
   {
      stream << "addr=" << addr_ << spaces(2);
      stream << "size=" << size_ << spaces(2);
      stream << "slab=" << slab_ << spaces(2);
      stream << "state=" << state_;
   }

   void* const addr_;   // area's address
   const size_t size_;  // area's size
   SlabId slab_;        // slab of which area is a part
   AreaState state_;    // area's state
};

//------------------------------------------------------------------------------
//
//  For tracking an area that is available for allocation.
//
struct AvailInfo
{
   AvailInfo(void* addr, size_t size) : addr_(addr), size_(size) { }

   void Print(ostream& stream) const
   {
      stream << "addr=" << addr_ << spaces(2);
      stream << "size=" << size_;
   }

   void* const addr_;   // area's address
   const size_t size_;  // area's size
};

//==============================================================================
//
//> The default size of a slab.
//
constexpr size_t SlabSize = 8 * MBs;

//  Types of corruption that can be detected.
//
enum SlabCorruptionReason
{
   FreeAreaNotInAvail,  // area is FREE but wasn't found in avail_
   FreeAreaNotInAreas   // area is FREE but wasn't found in areas_
};

//==============================================================================
//
//  Heap management information.
//
//  NOTE: This data is currently allocated on the default heap, even though
//  ====  it should reside in the same MemoryType as that managed by the heap.
//        This would be important for a write-protected heap, but it would mean
//        using the corresponding custom allocator (e.g. ProtectedAllocator)
//        for std::vector, std::map, and std::multimap.
//
class SlabPriv
{
public:
   //  Constructor.
   //
   explicit SlabPriv(MemoryType type);

   //  Destructor.
   //
   ~SlabPriv();

   //  Deleted to prohibit copying.
   //
   SlabPriv(const SlabPriv& that) = delete;

   //  Deleted to prohibit copy assignment.
   //
   SlabPriv& operator=(const SlabPriv& that) = delete;

   //  Allocates a block of SIZE.
   //
   void* Alloc(size_t size);

   //  If ADDR is an in-use block, returns its size, else returns 0.
   //
   size_t BlockToSize(const void* addr) const;

   //  Returns the amount of memory currently available.
   //
   size_t CurrAvail() const;

   //  Displays heap information.
   //
   void Display(ostream& stream,
      const string& prefix, const Flags& options) const;

   //  Frees the block at ADDR.  Returns false if ADDR is not an in-use
   //  block.
   //
   bool Free(const void* addr);

   //  Returns the number of bytes of heap management overhead.
   //
   size_t Overhead() const;

   //  Applies ATTRS to the heap.  Returns 0 on success.  On failure,
   //  initiates a restart and returns an error code.
   //
   int SetPermissions(MemoryProtection attrs) const;

   //  Sets the size of the slabs that will be allocated.
   //
   void SetSlabSize(size_t size);

   //  Returns the number of bytes allocated for the heap.
   //
   size_t Size() const;

   //  Returns the type of memory managed by the heap.
   //
   MemoryType Type() const { return type_; }

   //  Validates the heap.  If ADDR is not nullptr, only the block that
   //  is alleged to be at ADDR is validated.
   //
   bool Validate(const void* addr) const;
private:
   //  Adds a slab when there isn't a free area that can satisfy an
   //  allocation request.  Returns false if allocation fails.
   //
   bool Extend();

   //  Removes AREA from the set of available areas.
   //
   void EraseFromFree(const AreaInfo& area);

   //  Invoked when corruption is detected.  REASON specifies the type
   //  of corruption, and RESTART is set to initiate a restart.
   //
   void Corrupt(int reason, bool restart) const;

   //  The type of memory that the heap manages.
   //
   const MemoryType type_;

   //  The size of each slab.
   //
   size_t size_;

   //  For locking the heap during operations.
   //
   std::unique_ptr<SysMutex> mutex_;

   //  The slabs allocated for the heap.
   //
   std::vector<SlabInfo> slabs_;

   //  All areas, sorted by address.
   //
   std::map<const void*, AreaInfo> areas_;

   //  Available areas, grouped by size.
   //
   std::multimap<size_t, AvailInfo> avail_;
};

typedef std::pair<const void*, AreaInfo> AddrPair;  //* to be deleted
typedef std::pair<size_t, AvailInfo> SizePair;       //* to be deleted

//------------------------------------------------------------------------------

SlabPriv::SlabPriv(MemoryType type) : type_(type), size_(SlabSize)
{
   Debug::ft("SlabPriv.ctor");

   //  Allocate the mutex.
   //
   std::ostringstream stream;
   stream << "SlabLock(" << type_ << ')';
   mutex_.reset(new SysMutex(stream.str().c_str()));

   if(mutex_ == nullptr)
   {
      Restart::Initiate(RestartWarm, MutexCreationFailed, 0);
   }
}

//------------------------------------------------------------------------------

SlabPriv::~SlabPriv()
{
   Debug::ft("SlabPriv.dtor");

   mutex_->Acquire(TIMEOUT_NEVER);

   //  Free each slab.
   //
   for(auto s = slabs_.cbegin(); s != slabs_.cend(); ++s)
   {
      SysMemory::Protect(s->addr_, s->size_, MemReadWrite);
      SysMemory::Free(s->addr_, s->size_);
   }

   mutex_->Release();
}

//------------------------------------------------------------------------------

void* SlabPriv::Alloc(size_t size)
{
   Debug::ft("SlabPriv.Alloc");

   //  Find the smallest available area that can satisfy the request.
   //  If no area is available, allocate a slab and try again.
   //
   if(size > size_) return nullptr;

   MutexGuard guard(mutex_.get());

   auto avail = avail_.lower_bound(size);

   if(avail == avail_.cend())
   {
      if(!Extend()) return nullptr;
      return Alloc(size);
   }

   //  We have an area.  If it has more space than required, split it
   //  and make the free portion available, and also split it within
   //  areas_.
   //
   auto addr = avail->second.addr_;
   auto total = avail->second.size_;
   auto extra = total - size;

   auto block = areas_.find(addr);
   if(block == areas_.cend())
   {
      Corrupt(FreeAreaNotInAreas, true);
      return nullptr;
   }

   //  Remove the block that was allocated.  If it has more space than
   //  was requested, split it and make the free portion available, and
   //  also split it within areas_.  If it has no extra space, just
   //  mark it in use within areas_.
   //
   avail_.erase(avail);

   if(extra > 0)
   {
      auto succ = (void*) (uintptr_t(addr) + size);
      avail_.insert(SizePair(extra, AvailInfo(succ, extra)));

      auto slab = block->second.slab_;
      areas_.erase(block);
      areas_.insert(AddrPair(addr, AreaInfo(addr, size, slab, USED)));
      areas_.insert(AddrPair(succ, AreaInfo(succ, extra, slab, FREE)));
   }
   else
   {
      block->second.state_ = USED;
   }

   return addr;
}

//------------------------------------------------------------------------------

size_t SlabPriv::BlockToSize(const void* addr) const
{
   Debug::ft("SlabPriv.BlockToSize");

   auto block = areas_.find(addr);
   if(block == areas_.cend()) return 0;
   return (block->second.state_ == USED ? block->second.size_ : 0);
}

//------------------------------------------------------------------------------

void SlabPriv::Corrupt(int reason, bool restart) const
{
   if(restart && !Element::RunningInLab())
      Restart::Initiate(Restart::LevelToClear(type_), HeapCorruption, reason);
   else
      Debug::SwLog("SlabPriv.Corrupt", "slab corruption", reason);
}

//------------------------------------------------------------------------------

size_t SlabPriv::CurrAvail() const
{
   size_t total = 0;

   MutexGuard guard(mutex_.get());

   for(auto a = avail_.cbegin(); a != avail_.cend(); ++a)
   {
      total += a->second.size_;
   }

   return total;
}

//------------------------------------------------------------------------------

void SlabPriv::Display(ostream& stream,
   const string& prefix, const Flags& options) const
{
   MutexGuard guard(mutex_.get());

   auto verbose = options.test(DispVerbose);
   auto indent = prefix + spaces(2);

   stream << prefix << "type  : " << type_ << CRLF;
   stream << prefix << "mutex : " << CRLF;
   mutex_->Display(stream, indent, options);

   stream << prefix << "slabs : " << slabs_.size() << CRLF;

   if(verbose)
   {
      size_t i = 0;

      for(auto s = slabs_.cbegin(); s != slabs_.cend(); ++s, ++i)
      {
         stream << indent << strIndex(i, 0, true);
         s->Print(stream);
         stream << CRLF;
      }
   }

   stream << prefix << "avail : " << avail_.size() << CRLF;

   if(verbose)
   {
      size_t i = 0;

      for(auto a = avail_.cbegin(); a != avail_.cend(); ++a, ++i)
      {
         stream << indent << strIndex(i, 0, true);
         a->second.Print(stream);
         stream << CRLF;
      }
   }

   stream << prefix << "areas : " << areas_.size() << CRLF;

   if(verbose)
   {
      size_t i = 0;

      for(auto u = areas_.cbegin(); u != areas_.cend(); ++u, ++i)
      {
         stream << indent << strIndex(i, 0, true);
         u->second.Print(stream);
         stream << CRLF;
      }
   }
}

//------------------------------------------------------------------------------

void SlabPriv::EraseFromFree(const AreaInfo& area)
{
   Debug::ft("SlabPriv.EraseFromFree");

   //  AREA is free, so it must also appear in the set of available areas.
   //
   auto range = avail_.equal_range(area.size_);

   for(auto& f = range.first; f != range.second; ++f)
   {
      if(f->second.addr_ == area.addr_)
      {
         avail_.erase(f);
         return;
      }
   }

   Corrupt(FreeAreaNotInAvail, true);
}

//------------------------------------------------------------------------------

bool SlabPriv::Extend()
{
   Debug::ft("SlabPriv.Extend");

   //  Allocate a new slab, record it, and add it to the available areas.
   //
   auto id = slabs_.size();
   if(id > MaxSlab) return false;
   auto addr = SysMemory::Alloc(nullptr, size_);
   if(addr == nullptr) return false;
   slabs_.push_back(SlabInfo(addr, size_));
   areas_.insert(AddrPair(addr, AreaInfo(addr, size_, id, FREE)));
   avail_.insert(SizePair(size_, AvailInfo(addr, size_)));
   return true;
}

//------------------------------------------------------------------------------

bool SlabPriv::Free(const void* addr)
{
   Debug::ft("SlabPriv.Free");

   MutexGuard guard(mutex_.get());

   auto curr = areas_.find(addr);
   if(curr == areas_.cend()) return false;
   if(curr->second.state_ != USED) return false;

   //  See if CURR can merge with its predecessor and/or successor before
   //  finalizing the area that is available.
   //
   auto avail = curr->second.addr_;
   auto size = curr->second.size_;
   auto slab = curr->second.slab_;
   auto succ = std::next(curr);
   auto merged = false;

   if(curr != areas_.cbegin())
   {
      auto pred = std::prev(curr);

      if((pred->second.state_ == FREE) && (pred->second.slab_ == slab))
      {
         //  CURR and PRED will merge, but they might also merge with SUCC.
         //  For now, erase CURR and PRED and update the address and size
         //  for the merged area.
         //
         merged = true;
         avail = pred->second.addr_;
         size += pred->second.size_;
         EraseFromFree(pred->second);
         curr = areas_.erase(pred);
         succ = areas_.erase(curr);
      }
   }

   if((succ != areas_.cend()) && (succ->second.state_ == FREE) &&
      (succ->second.slab_ == slab))
   {
      size += succ->second.size_;

      //  Erase SUCC.  If MERGED is set, CURR was already erased above,
      //  else it must also be erased.
      //
      if(!merged)
      {
         succ = areas_.erase(curr);
      }

      merged = true;
      EraseFromFree(succ->second);
      areas_.erase(succ);
   }

   //  We now have the address and size of the available area, which may
   //  have merged with its predecessor and successor.  If MERGED is not
   //  set, CURR wasn't erase, so just update its state.
   //
   if(!merged)
      curr->second.state_ = FREE;
   else
      areas_.insert(AddrPair(avail, AreaInfo(avail, size, slab, FREE)));

   avail_.insert(SizePair(size, AvailInfo(avail, size)));
   return true;
}

//------------------------------------------------------------------------------

size_t SlabPriv::Overhead() const
{
   //  This is approximate and assumes an overhead of 4 pointers per entry
   //  (left, right, parent, and color data for nodes in a red-black tree).
   //
   size_t size = sizeof(SlabPriv);
   size += slabs_.size() * sizeof(SlabInfo);
   size += avail_.size() * (4 * sizeof(void*) + sizeof(AreaInfo));
   size += areas_.size() * (4 * sizeof(void*) + sizeof(AreaInfo));
   return size;
}

//------------------------------------------------------------------------------

int SlabPriv::SetPermissions(MemoryProtection attrs) const
{
   Debug::ft("SlabPriv.SetPermissions");

   //  This hasn't been tested because it shouldn't be used until SlabPriv
   //  is allocated in the same MemoryType that it manages.  It is currently
   //  allocated on the default heap, which is OK because it isn't yet used
   //  when write-protection is required.
   //
   for(auto s = slabs_.cbegin(); s != slabs_.cend(); ++s)
   {
      auto err = SysMemory::Protect(s->addr_, s->size_, attrs);

      if(err != 0)
      {
         Restart::Initiate
            (Restart::LevelToClear(Type()), HeapProtectionFailed, err);
         return err;
      }
   }

   return 0;
}

//------------------------------------------------------------------------------

fn_name SlabPriv_SetSlabSize = "SlabPriv.SetSlabSize";

void SlabPriv::SetSlabSize(size_t size)
{
   Debug::ft(SlabPriv_SetSlabSize);

   if(slabs_.empty())
   {
      if(size < 32)
      {
         Debug::SwLog(SlabPriv_SetSlabSize, "increasing slab size to 32", 0);
         size = 32;
      }

      size_ = size;
      return;
   }

   Debug::SwLog(SlabPriv_SetSlabSize, "slab already allocated", size_);
}

//------------------------------------------------------------------------------

size_t SlabPriv::Size() const
{
   size_t total = 0;

   MutexGuard guard(mutex_.get());

   for(auto s = slabs_.cbegin(); s != slabs_.cend(); ++s)
   {
      total += s->size_;
   }

   return total;
}

//------------------------------------------------------------------------------

bool SlabPriv::Validate(const void* addr) const
{
   Debug::ft("SlabPriv.Validate");

   //* Iterate over areas_ to verify that all memory in slabs_ is accounted
   //  for with no gaps or overlaps, and that an unused area also appears in
   //  avail_.
   //
   return true;
}

//==============================================================================

SlabHeap::SlabHeap(MemoryType type) : Heap(),
   priv_(new SlabPriv(type))
{
   Debug::ft("SlabHeap.ctor");

   Debug::Assert(priv_ != nullptr);
}

//------------------------------------------------------------------------------

SlabHeap::~SlabHeap()
{
   Debug::ftnt("SlabHeap.dtor");
}

//------------------------------------------------------------------------------

void* SlabHeap::Alloc(size_t size)
{
   Debug::ft("SlabHeap.Alloc");

   auto addr = priv_->Alloc(size);
   Requested(size, addr);
   return addr;
}

//------------------------------------------------------------------------------

size_t SlabHeap::BlockToSize(const void* addr) const
{
   Debug::ft("SlabHeap.BlockToSize");

   return priv_->BlockToSize(addr);
}

//------------------------------------------------------------------------------

size_t SlabHeap::CurrAvail() const
{
   return priv_->CurrAvail();
}

//------------------------------------------------------------------------------

void SlabHeap::Display(ostream& stream,
   const string& prefix, const Flags& options) const
{
   Heap::Display(stream, prefix, options);

   stream << prefix << "priv : " << priv_.get() << CRLF;

   if(priv_ != nullptr)
   {
      priv_->Display(stream, prefix, options);
   }
}

//------------------------------------------------------------------------------

void SlabHeap::Free(void* addr)
{
   Debug::ft("SlabHeap.Free");

   auto size = BlockToSize(addr);
   if(size == 0) return;

   if(priv_->Free(addr))
   {
      Freeing(addr, size);
   }
}

//------------------------------------------------------------------------------

size_t SlabHeap::Overhead() const
{
   return priv_->Overhead();
}

//------------------------------------------------------------------------------

void SlabHeap::Patch(sel_t selector, void* arguments)
{
   Object::Patch(selector, arguments);
}

//------------------------------------------------------------------------------

int SlabHeap::SetPermissions(MemoryProtection attrs)
{
   Debug::ft("SlabHeap.SetPermissions");

   if(GetAttrs() == attrs) return 0;
   auto err = priv_->SetPermissions(attrs);
   return (err == 0 ? SetAttrs(attrs) : err);
}

//------------------------------------------------------------------------------

void SlabHeap::SetSlabSize(size_t size)
{
   Debug::ft("SlabHeap.SetSlabSize");

   priv_->SetSlabSize(size);
}

//------------------------------------------------------------------------------

size_t SlabHeap::Size() const
{
   return priv_->Size();
}

//------------------------------------------------------------------------------

MemoryType SlabHeap::Type() const
{
   return priv_->Type();
}

//------------------------------------------------------------------------------

bool SlabHeap::Validate(const void* addr) const
{
   Debug::ft("SlabHeap.Validate");

   return priv_->Validate(addr);
}
}