//==============================================================================
//
//  CxxVector.h
//
//  Copyright (C) 2013-2021  Greg Utas
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
#ifndef CXXVECTOR_H_INCLUDED
#define CXXVECTOR_H_INCLUDED

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "Base.h"
#include "CxxNamed.h"
#include "SysTypes.h"

//------------------------------------------------------------------------------
//
//  Function templates for vectors.
//
namespace CodeTools
{
//  Returns the index of ITEM in VEC.  Returns SIZE_MAX if ITEM isn't found.
//
template< typename T > size_t IndexOf
   (const std::vector< T >& vec, const T& item)
{
   for(size_t i = 0; i < vec.size(); ++i)
   {
      if(vec[i] == item) return i;
   }

   return SIZE_MAX;
}

//------------------------------------------------------------------------------
//
//  Displays the objects in VEC.
//
template< class T > void DisplayObjects
   (const std::vector< T >& vec, std::ostream& stream,
   const std::string& prefix, const NodeBase::Flags& options)
{
   for(auto i = vec.cbegin(); i != vec.cend(); ++i)
   {
      (*i)->Display(stream, prefix, options);
   }
}

//------------------------------------------------------------------------------
//
//  Copies the objects in VEC so they can be sorted by position and displayed.
//  FILE is set to sort the objects by file and position.
//
template< class T > void SortAndDisplayItems
   (const std::vector< T >& vec, std::ostream& stream,
   const std::string& prefix, const NodeBase::Flags& options, bool file = true)
{
   std::vector< T > v(vec);

   if(file)
      std::sort(v.begin(), v.end(), IsSortedByFilePos);
   else
      std::sort(v.begin(), v.end(), IsSortedByPos);

   for(auto i = v.cbegin(); i != v.cend(); ++i)
   {
      (*i)->Display(stream, prefix, options);
   }
}

//------------------------------------------------------------------------------
//
//  Copies the objects in VEC so they can be sorted by position and displayed.
//  FILE is set to sort the objects by file and position.
//
template< class T > void SortAndDisplayItemPtrs
   (const std::vector< std::unique_ptr< T >>& vec, std::ostream& stream,
   const std::string& prefix, const NodeBase::Flags& options, bool file = true)
{
   std::vector< T* > v;

   for(auto i = vec.cbegin(); i != vec.cend(); ++i)
   {
      v.push_back(i->get());
   }

   if(file)
      std::sort(v.begin(), v.end(), IsSortedByFilePos);
   else
      std::sort(v.begin(), v.end(), IsSortedByPos);

   for(auto i = v.cbegin(); i != v.cend(); ++i)
   {
      (*i)->Display(stream, prefix, options);
   }
}

//------------------------------------------------------------------------------
//
//  Removes ITEM from VEC and shifts the following items up.
//
template< class T > void EraseItem(std::vector< T* >& vec, const T* item)
{
   for(size_t i = 0; i < vec.size(); ++i)
   {
      if(vec[i] == item)
      {
         if(i < vec.size() - 1)
         {
            for(auto j = i + 1; j < vec.size(); ++j)
            {
               vec[j - 1] = vec[j];
            }
         }

         vec.pop_back();
         return;
      }
   }
}

//------------------------------------------------------------------------------
//
//  Removes ITEM from VEC and shifts the following items up.
//
template< class T > void EraseItemPtr
   (std::vector< std::unique_ptr< T >>& vec, const T* item)
{
   for(size_t i = 0; i < vec.size(); ++i)
   {
      if(vec[i].get() == item)
      {
         vec[i].release();

         if(i < vec.size() - 1)
         {
            for(auto j = i + 1; j < vec.size(); ++j)
            {
               vec[j - 1] = std::move(vec[j]);
            }
         }

         vec.pop_back();
         return;
      }
   }
}
}
#endif