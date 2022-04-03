//==============================================================================
//
//  Element.h
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
#ifndef ELEMENT_H_INCLUDED
#define ELEMENT_H_INCLUDED

#include "Protected.h"
#include <string>
#include "NbTypes.h"

//------------------------------------------------------------------------------

namespace NodeBase
{
//  Attributes of the element on which this software load is running.
//
class Element : public Protected
{
   friend class Singleton< Element >;
public:
   //  Deleted to prohibit copying.
   //
   Element(const Element& that) = delete;

   //  Deleted to prohibit copy assignment.
   //
   Element& operator=(const Element& that) = delete;

   //  Returns true if the element's name was not set, which probably means
   //  that the configuration file was not found.
   //
   static bool IsUnnamed();

   //  Returns the element's name.
   //
   static std::string Name();

   //  Returns a string containing the current time in SysTime::Alpha format,
   //  followed by " on " and Element::Name().
   //
   static std::string strTimePlace();

   //  Returns the first directory named "rsc" on the path to the .exe that
   //  contains the object code.  Does not include a trailing PATH_SEPARATOR
   //  character.
   //
   static const std::string RscPath();

   //  Returns the help directory, which contains help files for CLI commands
   //  and increments.  Does not include a trailing PATH_SEPARATOR character.
   //
   static const std::string HelpPath();

   //  Returns the input directory, which contains the element.config.txt file
   //  and files for the CLI >read command.  Does not include a trailing
   //  PATH_SEPARATOR character.
   //
   static const std::string InputPath();

   //  Returns the output directory, where files generated by the element are
   //  written.  Does not include a trailing PATH_SEPARATOR character.
   //
   static const std::string OutputPath();

   //  Returns the name for the console transcript file.
   //
   static const std::string ConsoleFileName();

   //  Returns true if running in a non-field (debug) load.
   //
   static bool RunningInLab();

   //  Overridden to display member variables.
   //
   void Display(std::ostream& stream,
      const std::string& prefix, const Flags& options) const override;

   //  Overridden for patching.
   //
   void Patch(sel_t selector, void* arguments) override;
private:
   //  Private because this is a singleton.
   //
   Element();

   //  Private because this is a singleton.
   //
   ~Element();

   //  Configuration parameter for the element's name.
   //
   CfgStrParmPtr nameCfg_;

   //  Configuration parameter that defines if this is a lab load.
   //
   CfgBoolParmPtr runningInLabCfg_;
};
}
#endif