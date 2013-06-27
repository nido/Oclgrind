#include "common.h"

#define __STDC_LIMIT_MACROS
#define __STDC_CONSTANT_MACROS
#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Function.h"
#include "llvm/Metadata.h"
#include "llvm/Module.h"
#include "llvm/Type.h"

#include "CL/cl.h"
#include "Kernel.h"
#include "Memory.h"

using namespace spirsim;
using namespace std;

Kernel::Kernel(const llvm::Function *function, const llvm::Module *module)
{
  m_function = function;
  m_localMemory = 0;

  m_globalSize[0] = 0;
  m_globalSize[1] = 0;
  m_globalSize[2] = 0;

  // Get name
  m_name = function->getName().str();

  // Get required work-group size from metadata
  memset(m_requiredWorkGroupSize, 0, sizeof(size_t[3]));
  llvm::NamedMDNode *mdKernels = module->getNamedMetadata("opencl.kernels");
  if (mdKernels)
  {
    llvm::MDNode *md = mdKernels->getOperand(0);
    for (int i = 0; i < md->getNumOperands(); i++)
    {
      llvm::Value *op = md->getOperand(i);
      if (op->getValueID() == llvm::Value::MDNodeVal)
      {
        llvm::MDNode *val = ((llvm::MDNode*)op);
        string name = val->getOperand(0)->getName().str();
        if (name == "reqd_work_group_size")
        {
          for (int j = 0; j < 3; j++)
          {
            m_requiredWorkGroupSize[j] =
              ((const llvm::ConstantInt*)val->getOperand(j+1))->getZExtValue();
          }
        }
      }
    }
  }

  // Set-up global variables
  llvm::Module::const_global_iterator itr;
  for (itr = module->global_begin(); itr != module->global_end(); itr++)
  {
    llvm::PointerType *type = itr->getType();
    if (type->getPointerAddressSpace() == AddrSpaceLocal)
    {
      size_t size = getTypeSize(itr->getInitializer()->getType());
      TypedValue v = {
        sizeof(size_t),
        1,
        new unsigned char[sizeof(size_t)]
      };
      *((size_t*)v.data) = m_localMemory;
      m_localMemory += size;
      m_arguments[itr] = v;
    }
    if (itr->isConstant())
    {
      m_constants.push_back(itr);
    }
  }
}

Kernel::~Kernel()
{
}

void Kernel::allocateConstants(Memory *memory)
{
  list<const llvm::GlobalVariable*>::const_iterator itr;
  for (itr = m_constants.begin(); itr != m_constants.end(); itr++)
  {
    const llvm::Constant *initializer = (*itr)->getInitializer();
    const llvm::Type *type = initializer->getType();

    // Allocate buffer
    size_t size = getTypeSize(type);
    TypedValue v = {
      sizeof(size_t),
      1,
      new unsigned char[sizeof(size_t)]
    };
    size_t address = memory->allocateBuffer(size);
    *((size_t*)v.data) = address;
    m_arguments[*itr] = v;

    // Initialise buffer contents
    if (type->isArrayTy())
    {
      int num = type->getArrayNumElements();
      const llvm::Type *elemType = type->getArrayElementType();
      size_t elemSize = getTypeSize(elemType);
      for (int i = 0; i < num; i++)
      {
        storeConstant(memory, address + i*elemSize,
                      initializer->getAggregateElement(i));
      }
    }
    else if (type->isPrimitiveType())
    {
      storeConstant(memory, address, (const llvm::Constant*)initializer);
    }
    else
    {
      cerr << "Unhandled constant buffer type " << type->getTypeID() << endl;
    }
  }
}

void Kernel::deallocateConstants(Memory *memory) const
{
  // TODO: Add support for deallocation in Memory class
}

const llvm::Argument* Kernel::getArgument(unsigned int index) const
{
  assert(index < getNumArguments());

  llvm::Function::const_arg_iterator argItr = m_function->arg_begin();
  for (int i = 0; i < index; i++)
  {
    argItr++;
  }
  return argItr;
}

size_t Kernel::getArgumentSize(unsigned int index) const
{
  const llvm::Type *type = getArgument(index)->getType();

  // Check if pointer argument
  if (type->isPointerTy())
  {
    return sizeof(size_t);
  }

  return getTypeSize(type);
}

unsigned int Kernel::getArgumentType(unsigned int index) const
{
  const llvm::Type *type = getArgument(index)->getType();

  // Check if scalar argument
  if (!type->isPointerTy())
  {
    return CL_KERNEL_ARG_ADDRESS_PRIVATE;
  }

  // Check address space
  unsigned addressSpace = type->getPointerAddressSpace();
  switch (addressSpace)
  {
  case AddrSpaceGlobal:
    return CL_KERNEL_ARG_ADDRESS_GLOBAL;
  case AddrSpaceConstant:
    return CL_KERNEL_ARG_ADDRESS_CONSTANT;
  case AddrSpaceLocal:
    return CL_KERNEL_ARG_ADDRESS_LOCAL;
  default:
    cerr << "Unrecognized address space " << addressSpace << endl;
    return 0;
  }
}

const llvm::Function* Kernel::getFunction() const
{
  return m_function;
}

const size_t* Kernel::getGlobalSize() const
{
  return m_globalSize;
}

size_t Kernel::getLocalMemorySize() const
{
  return m_localMemory;
}


const std::string& Kernel::getName() const
{
  return m_name;
}

unsigned int Kernel::getNumArguments() const
{
  return m_function->arg_size();
}

const size_t* Kernel::getRequiredWorkGroupSize() const
{
  return m_requiredWorkGroupSize;
}

void Kernel::setArgument(unsigned int index, TypedValue value)
{
  if (index >= m_function->arg_size())
  {
    cerr << "Argument index out of range." << endl;
    return;
  }

  unsigned int type = getArgumentType(index);
  if (type == CL_KERNEL_ARG_ADDRESS_LOCAL)
  {
    TypedValue v = {
      value.size,
      value.num,
      new unsigned char[value.size*value.num]
    };
    *((size_t*)v.data) = m_localMemory;
    m_localMemory += value.size;

    m_arguments[getArgument(index)] = v;
  }
  else
  {
    const llvm::Type *type = getArgument(index)->getType();
    if (type->isVectorTy())
    {
      value.num = type->getVectorNumElements();
      value.size /= value.num;
    }
    m_arguments[getArgument(index)] = clone(value);
  }
}

void Kernel::setGlobalSize(const size_t globalSize[3])
{
  m_globalSize[0] = globalSize[0];
  m_globalSize[1] = globalSize[1];
  m_globalSize[2] = globalSize[2];
}

void Kernel::storeConstant(Memory *memory, size_t address,
                           const llvm::Constant *constant) const
{
  const llvm::Type *type = constant->getType();
  size_t size = getTypeSize(type);
  switch (type->getTypeID())
  {
  case llvm::Type::IntegerTyID:
    memory->store(address, size,
                  (const unsigned char*)
                  ((llvm::ConstantInt*)constant)->getValue().getRawData());
    break;
  case llvm::Type::FloatTyID:
  {
    float f = ((llvm::ConstantFP*)constant)->getValueAPF().convertToFloat();
    memory->store(address, size, (const unsigned char*)&f);
    break;
  }
  default:
    cerr << "Unhandled constant type "
         << type->getTypeID() << endl;
    break;
  }
}

TypedValueMap::const_iterator Kernel::args_begin() const
{
  return m_arguments.begin();
}

TypedValueMap::const_iterator Kernel::args_end() const
{
  return m_arguments.end();
}
