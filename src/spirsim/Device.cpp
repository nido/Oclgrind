// Device.cpp (oclgrind)
// Copyright (C) 2013 James Price
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, see <http://www.gnu.org/licenses/>.

#include "common.h"
#include <istream>
#include <iterator>
#include <sstream>

#include "Kernel.h"
#include "Memory.h"
#include "Device.h"
#include "WorkGroup.h"

using namespace spirsim;
using namespace std;

Device::Device()
{
  m_globalMemory = new Memory();
  m_interactive = false;

  // Check for interactive environment variable
  const char *env = getenv("OCLGRIND_INTERACTIVE");
  if (env && strcmp(env, "1") == 0)
  {
    m_interactive = true;
  }

  // Set-up interactive commands
#define ADD_CMD(name, sname, func)  \
  m_commands[name] = &Device::func; \
  m_commands[sname] = &Device::func;
  ADD_CMD("backtrace",    "bt", backtrace);
  ADD_CMD("break",        "b",  brk);
  ADD_CMD("clear",        "cl", clear);
  ADD_CMD("continue",     "c",  cont);
  ADD_CMD("help",         "h",  help);
  ADD_CMD("list",         "l",  list);
  ADD_CMD("print",        "p",  print);
  ADD_CMD("printglobal",  "pg", printglobal);
  ADD_CMD("printlocal",   "pl", printlocal);
  ADD_CMD("printprivate", "pp", printprivate);
  ADD_CMD("quit",         "q",  quit);
  ADD_CMD("step",         "s",  step);
  ADD_CMD("workitem",     "wi", workitem);
}

Device::~Device()
{
  delete m_globalMemory;
}

Memory* Device::getGlobalMemory() const
{
  return m_globalMemory;
}

void Device::run(Kernel& kernel, unsigned int workDim,
                 const size_t *globalOffset,
                 const size_t *globalSize,
                 const size_t *localSize)
{
  // Set-up offsets and sizes
  size_t offset[3] = {0,0,0};
  size_t ndrange[3] = {1,1,1};
  size_t wgsize[3] = {1,1,1};
  for (int i = 0; i < workDim; i++)
  {
    ndrange[i] = globalSize[i];
    if (globalOffset[i])
    {
      offset[i] = globalOffset[i];
    }
    if (localSize[i])
    {
      wgsize[i] = localSize[i];
    }
  }

  // Allocate and initialise constant memory
  kernel.allocateConstants(m_globalMemory);

  // Create work-groups
  m_numGroups[0] = ndrange[0]/wgsize[0];
  m_numGroups[1] = ndrange[1]/wgsize[1];
  m_numGroups[2] = ndrange[2]/wgsize[2];
  size_t totalNumGroups = m_numGroups[0]*m_numGroups[1]*m_numGroups[2];
  m_workGroups = new WorkGroup*[totalNumGroups];
  for (int k = 0; k < m_numGroups[2]; k++)
  {
    for (int j = 0; j < m_numGroups[1]; j++)
    {
      for (int i = 0; i < m_numGroups[0]; i++)
      {
        m_workGroups[i + (k*m_numGroups[1] + j)*m_numGroups[0]] =
          new WorkGroup(kernel, *m_globalMemory,
                        workDim, i, j, k, offset, ndrange, wgsize);
      }
    }
  }

  // Check if we're in interactive mode
  if (m_interactive)
  {
    m_running = true;
  }
  else
  {
    // If not, just run kernel
    cont(vector<string>());
    m_running = false;
  }

  // Interactive debugging loop
  while (m_running)
  {
    // Prompt for command
    string cmd;
    cout << ">> " << std::flush;
    getline(cin, cmd);

    // Split command into tokens
    vector<string> tokens;
    istringstream iss(cmd);
    copy(istream_iterator<string>(iss),
         istream_iterator<string>(),
         back_inserter< vector<string> >(tokens));

    // Check for end of stream or empty command
    if (cin.eof())
    {
      quit(tokens);
    }
    if (tokens.size() == 0)
    {
      continue;
    }

    // Find command in map and execute
    map<string,Command>::iterator itr = m_commands.find(tokens[0]);
    if (itr != m_commands.end())
    {
      (this->*itr->second)(tokens);
    }
    else
    {
      cout << "Unrecognized command '" << tokens[0] << "'" << endl;
    }
  }

  // Destroy work-groups
  for (int i = 0; i < totalNumGroups; i++)
  {
    delete m_workGroups[i];
  }
  delete[] m_workGroups;

  // Deallocate constant memory
  kernel.deallocateConstants(m_globalMemory);
}


////////////////////////////////
//// Interactive Debugging  ////
////////////////////////////////

void Device::backtrace(vector<string> args)
{
  // TODO: Implement
  cout << "Unimplemented command 'backtrace'" << endl;
}

void Device::brk(vector<string> args)
{
  // TODO: Implement
  cout << "Unimplemented command 'brk'" << endl;
}

void Device::clear(vector<string> args)
{
  // TODO: Implement
  cout << "Unimplemented command 'clear'" << endl;
}

void Device::cont(vector<string> args)
{
  // TODO: Implement properly
  for (int k = 0; k < m_numGroups[2]; k++)
  {
    for (int j = 0; j < m_numGroups[1]; j++)
    {
      for (int i = 0; i < m_numGroups[0]; i++)
      {
        WorkGroup *workGroup =
          m_workGroups[i + (k*m_numGroups[1] + j)*m_numGroups[0]];
        workGroup->run();
      }
    }
  }
  m_running = false;
}

void Device::help(vector<string> args)
{
  if (args.size() < 2)
  {
    cout << "Command list:" << endl;
    cout << "  backtrace    (bt)" << endl;
    cout << "  break        (b)" << endl;
    cout << "  clear        (cl)" << endl;
    cout << "  continue     (c)" << endl;
    cout << "  help         (h)" << endl;
    cout << "  list         (l)" << endl;
    cout << "  print        (p)" << endl;
    cout << "  printglobal  (pg)" << endl;
    cout << "  printlocal   (pl)" << endl;
    cout << "  printprivate (pp)" << endl;
    cout << "  quit         (q)" << endl;
    cout << "  step         (s)" << endl;
    cout << "  workitem     (wi)" << endl;
    cout << "(type 'help command' for more information)" << endl;
    return;
  }

  if (args[1] == "backtrace")
  {
    // TODO: Help message
  }
  else if (args[1] == "break")
  {
    // TODO: Help message
  }
  else if (args[1] == "clear")
  {
    // TODO: Help message
  }
  else if (args[1] == "continue")
  {
    cout << "Continue kernel execution until next breakpoint." << endl;
  }
  else if (args[1] == "help")
  {
    cout << "Display usage information for a command." << endl;
  }
  else if (args[1] == "list")
  {
    // TODO: Help message
  }
  else if (args[1] == "print")
  {
    // TODO: Help message
  }
  else if (args[1] == "printglobal")
  {
    // TODO: Help message
  }
  else if (args[1] == "printlocal")
  {
    // TODO: Help message
  }
  else if (args[1] == "printprivate")
  {
    // TODO: Help message
  }
  else if (args[1] == "quit")
  {
    cout << "Quit interactive debugger "
        << "(and terminate current kernel invocation)." << endl;
  }
  else if (args[1] == "step")
  {
    // TODO: Help message
  }
  else if (args[1] == "workitem")
  {
    // TODO: Help message
  }
  else
  {
    cout << "Unrecognized command '" << args[1] << "'" << endl;
  }
}

void Device::list(vector<string> args)
{
  // TODO: Implement
  cout << "Unimplemented command 'list'" << endl;
}

void Device::print(vector<string> args)
{
  // TODO: Implement
  cout << "Unimplemented command 'print'" << endl;
}

void Device::printglobal(vector<string> args)
{
  // TODO: Implement
  cout << "Unimplemented command 'printglobal'" << endl;
}

void Device::printlocal(vector<string> args)
{
  // TODO: Implement
  cout << "Unimplemented command 'printlocal'" << endl;
}

void Device::printprivate(vector<string> args)
{
  // TODO: Implement
  cout << "Unimplemented command 'printprivate'" << endl;
}

void Device::quit(vector<string> args)
{
  m_interactive = false;
  m_running = false;
}

void Device::step(vector<string> args)
{
  // TODO: Implement
  cout << "Unimplemented command 'step'" << endl;
}

void Device::workitem(vector<string> args)
{
  // TODO: Implement
  cout << "Unimplemented command 'workitem'" << endl;
}
