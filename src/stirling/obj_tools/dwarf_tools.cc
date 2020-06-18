#include "src/stirling/obj_tools/dwarf_tools.h"

#include <llvm/DebugInfo/DIContext.h>
#include <llvm/Object/ObjectFile.h>

#include "src/stirling/obj_tools/init.h"

#define LLVM_ASSIGN_OR_RETURN_IMPL(var, tmpvar, expr, err) \
  auto tmpvar = expr;                                      \
  if (!tmpvar.hasValue()) {                                \
    return error::Internal(err);                           \
  }                                                        \
  var = tmpvar.getValue();

#define LLVM_ASSIGN_OR_RETURN(var, expr, err) \
  LLVM_ASSIGN_OR_RETURN_IMPL(var, PL_CONCAT_NAME(__t__, __COUNTER__), expr, err)

namespace pl {
namespace stirling {
namespace dwarf_tools {

using llvm::DWARFContext;
using llvm::DWARFDie;
using llvm::DWARFFormValue;

// TODO(oazizi): This will break on 32-bit binaries.
// Use ELF to get the correct value.
// https://superuser.com/questions/791506/how-to-determine-if-a-linux-binary-file-is-32-bit-or-64-bit
uint8_t kAddressSize = sizeof(void*);

StatusOr<std::unique_ptr<DwarfReader>> DwarfReader::Create(std::string_view obj_filename,
                                                           bool index) {
  using llvm::MemoryBuffer;

  std::error_code ec;

  llvm::ErrorOr<std::unique_ptr<MemoryBuffer>> buff_or_err =
      MemoryBuffer::getFileOrSTDIN(std::string(obj_filename));
  ec = buff_or_err.getError();
  if (ec) {
    return error::Internal(ec.message());
  }

  std::unique_ptr<llvm::MemoryBuffer> buffer = std::move(buff_or_err.get());
  llvm::Expected<std::unique_ptr<llvm::object::Binary>> bin_or_err =
      llvm::object::createBinary(*buffer);
  ec = errorToErrorCode(bin_or_err.takeError());
  if (ec) {
    return error::Internal(ec.message());
  }

  auto* obj_file = llvm::dyn_cast<llvm::object::ObjectFile>(bin_or_err->get());
  if (!obj_file) {
    return error::Internal("Could not create DWARFContext.");
  }

  auto dwarf_reader = std::unique_ptr<DwarfReader>(
      new DwarfReader(std::move(buffer), DWARFContext::create(*obj_file)));
  if (index) {
    dwarf_reader->IndexStructs();
  }

  return dwarf_reader;
}

DwarfReader::DwarfReader(std::unique_ptr<llvm::MemoryBuffer> buffer,
                         std::unique_ptr<llvm::DWARFContext> dwarf_context)
    : memory_buffer_(std::move(buffer)), dwarf_context_(std::move(dwarf_context)) {
  // Only very first call will actually perform initialization.
  InitLLVMOnce();
}

namespace {

bool IsMatchingTag(llvm::dwarf::Tag tag, const DWARFDie& die) {
  llvm::dwarf::Tag die_tag = die.getTag();
  return (tag == static_cast<llvm::dwarf::Tag>(llvm::dwarf::DW_TAG_invalid) || (tag == die_tag));
}

bool IsMatchingDIE(std::string_view name, llvm::dwarf::Tag tag, const DWARFDie& die) {
  if (!IsMatchingTag(tag, die)) {
    // Not the right type.
    return false;
  }

  const char* die_short_name = die.getName(llvm::DINameKind::ShortName);

  // May also want to consider the linkage name (e.g. the mangled name).
  // That is what llvm-dwarfdebug appears to do.
  // const char* die_linkage_name = die.getName(llvm::DINameKind::LinkageName);

  return (die_short_name && name == die_short_name);
}

}  // namespace

Status DwarfReader::GetMatchingDIEs(DWARFContext::unit_iterator_range CUs, std::string_view name,
                                    llvm::dwarf::Tag tag, std::vector<DWARFDie>* dies_out) {
  for (const auto& CU : CUs) {
    for (const auto& Entry : CU->dies()) {
      DWARFDie die = {CU.get(), &Entry};
      if (IsMatchingDIE(name, tag, die)) {
        dies_out->push_back(std::move(die));
      }
    }
  }

  return Status::OK();
}

void DwarfReader::IndexStructs() {
  // For now, we only index structure types.
  // TODO(oazizi): Expand to cover other types, when needed.
  llvm::dwarf::Tag tag = llvm::dwarf::DW_TAG_structure_type;

  DWARFContext::unit_iterator_range CUs = dwarf_context_->normal_units();

  for (const auto& CU : CUs) {
    for (const auto& Entry : CU->dies()) {
      DWARFDie die = {CU.get(), &Entry};
      if (IsMatchingTag(tag, die)) {
        const char* die_short_name = die.getName(llvm::DINameKind::ShortName);
        if (die_short_name != nullptr) {
          // TODO(oazizi): What's the right way to deal with duplicate names?
          // Only appears to happen with structs like the following:
          //  ThreadStart, _IO_FILE, _IO_marker, G, in6_addr
          // So probably okay for now. But need to be wary of this.
          if (die_struct_map_.find(die_short_name) != die_struct_map_.end()) {
            VLOG(1) << "Duplicate name: " << die_short_name;
          }
          die_struct_map_[die_short_name] = die;
        }
      }
    }
  }
}

StatusOr<std::vector<DWARFDie>> DwarfReader::GetMatchingDIEs(std::string_view name,
                                                             llvm::dwarf::Tag type) {
  DCHECK(dwarf_context_ != nullptr);
  std::vector<DWARFDie> dies;

  // Special case for types that are indexed (currently only struct types);
  if (type == llvm::dwarf::DW_TAG_structure_type && !die_struct_map_.empty()) {
    auto iter = die_struct_map_.find(name);
    if (iter != die_struct_map_.end()) {
      return std::vector<DWARFDie>{iter->second};
    }
    return {};
  }

  PL_RETURN_IF_ERROR(GetMatchingDIEs(dwarf_context_->normal_units(), name, type, &dies));
  // TODO(oazizi): Might want to consider dwarf_context_->dwo_units() as well.

  return dies;
}

StatusOr<uint64_t> DwarfReader::GetStructMemberOffset(std::string_view struct_name,
                                                      std::string_view member_name) {
  PL_ASSIGN_OR_RETURN(std::vector<DWARFDie> dies,
                      GetMatchingDIEs(struct_name, llvm::dwarf::DW_TAG_structure_type));
  if (dies.empty()) {
    return error::Internal("Could not locate structure");
  }
  if (dies.size() > 1) {
    return error::Internal("Found too many DIE matches");
  }

  DWARFDie& struct_die = dies.front();

  for (const auto& die : struct_die.children()) {
    if ((die.getTag() == llvm::dwarf::DW_TAG_member) &&
        (die.getName(llvm::DINameKind::ShortName) == member_name)) {
      LLVM_ASSIGN_OR_RETURN(DWARFFormValue & attr,
                            die.find(llvm::dwarf::DW_AT_data_member_location),
                            "Found member, but could not find data_member_location attribute.");
      LLVM_ASSIGN_OR_RETURN(uint64_t offset, attr.getAsUnsignedConstant(),
                            "Could not extract offset.");
      return offset;
    }
  }

  return error::Internal("Could not find member.");
}

namespace {
StatusOr<uint64_t> GetBaseOrStructTypeByteSize(const DWARFDie& die) {
  DCHECK((die.getTag() == llvm::dwarf::DW_TAG_base_type) ||
         (die.getTag() == llvm::dwarf::DW_TAG_structure_type));

  LLVM_ASSIGN_OR_RETURN(DWARFFormValue & byte_size_attr, die.find(llvm::dwarf::DW_AT_byte_size),
                        "Could not find DW_AT_byte_size.");

  LLVM_ASSIGN_OR_RETURN(uint64_t byte_size, byte_size_attr.getAsUnsignedConstant(),
                        "Could not extract byte_size.");

  return byte_size;
}

StatusOr<uint64_t> GetTypeByteSize(const DWARFDie& die) {
  if (!die.isValid()) {
    return error::Internal("Encountered an invalid DIE.");
  }

  // If the type is a typedef, then follow the type and recursively call this function.
  switch (die.getTag()) {
    case llvm::dwarf::DW_TAG_typedef: {
      LLVM_ASSIGN_OR_RETURN(DWARFFormValue & type_attr, die.find(llvm::dwarf::DW_AT_type),
                            "Could not find DW_AT_type for function argument.");
      const auto& referenced_die = die.getAttributeValueAsReferencedDie(type_attr);
      return GetTypeByteSize(referenced_die);
    }
    case llvm::dwarf::DW_TAG_pointer_type:
      return kAddressSize;
    case llvm::dwarf::DW_TAG_base_type:
    case llvm::dwarf::DW_TAG_structure_type:
      return GetBaseOrStructTypeByteSize(die);
    default:
      return error::Internal(
          absl::Substitute("Unexpected DIE type: $0", magic_enum::enum_name(die.getTag())));
  }
}
}  // namespace

StatusOr<uint64_t> DwarfReader::GetArgumentTypeByteSize(std::string_view function_symbol_name,
                                                        std::string_view arg_name) {
  PL_ASSIGN_OR_RETURN(std::vector<DWARFDie> dies,
                      GetMatchingDIEs(function_symbol_name, llvm::dwarf::DW_TAG_subprogram));
  if (dies.empty()) {
    return error::Internal("Could not locate function symbol name.");
  }
  if (dies.size() > 1) {
    return error::Internal("Found too many DIE matches.");
  }

  DWARFDie& function_die = dies.front();

  for (const auto& die : function_die.children()) {
    if ((die.getTag() == llvm::dwarf::DW_TAG_formal_parameter) &&
        (die.getName(llvm::DINameKind::ShortName) == arg_name)) {
      LLVM_ASSIGN_OR_RETURN(DWARFFormValue & type_attr, die.find(llvm::dwarf::DW_AT_type),
                            "Could not find DW_AT_type for function argument.");
      return GetTypeByteSize(die.getAttributeValueAsReferencedDie(type_attr));
    }
  }
  return error::Internal("Could not find argument.");
}

// TODO(oazizi): Consider refactoring portions in common with GetArgumentTypeByteSize().
StatusOr<int64_t> DwarfReader::GetArgumentStackPointerOffset(std::string_view function_symbol_name,
                                                             std::string_view arg_name) {
  PL_ASSIGN_OR_RETURN(std::vector<DWARFDie> dies,
                      GetMatchingDIEs(function_symbol_name, llvm::dwarf::DW_TAG_subprogram));
  if (dies.empty()) {
    return error::Internal("Could not locate function symbol name.");
  }
  if (dies.size() > 1) {
    return error::Internal("Found too many DIE matches.");
  }

  DWARFDie& function_die = dies.front();

  for (const auto& die : function_die.children()) {
    if ((die.getTag() == llvm::dwarf::DW_TAG_formal_parameter) &&
        (die.getName(llvm::DINameKind::ShortName) == arg_name)) {
      LLVM_ASSIGN_OR_RETURN(DWARFFormValue & loc_attr, die.find(llvm::dwarf::DW_AT_location),
                            "Could not find DW_AT_location for function argument.");

      if (!loc_attr.isFormClass(DWARFFormValue::FC_Block) &&
          !loc_attr.isFormClass(DWARFFormValue::FC_Exprloc)) {
        return error::Internal("Unexpected Form: $0", magic_enum::enum_name(loc_attr.getForm()));
      }

      LLVM_ASSIGN_OR_RETURN(llvm::ArrayRef<uint8_t> loc, loc_attr.getAsBlock(),
                            "Could not extract location.");

      llvm::DataExtractor data(
          llvm::StringRef(reinterpret_cast<const char*>(loc.data()), loc.size()), true, 0);
      llvm::DWARFExpression expr(data, llvm::DWARFExpression::Operation::Dwarf4, kAddressSize);

      auto iter = expr.begin();
      auto& operation = *iter;
      ++iter;
      if (iter != expr.end()) {
        return error::Internal("Operation include more than one component. Not yet supported.");
      }

      auto code = static_cast<llvm::dwarf::LocationAtom>(operation.getCode());
      int64_t operand = operation.getRawOperand(0);
      VLOG(1) << absl::Substitute("code=$0 operand=$1", magic_enum::enum_name(code), operand);

      if (code == llvm::dwarf::LocationAtom::DW_OP_fbreg) {
        return operand;
      }
      if (code == llvm::dwarf::LocationAtom::DW_OP_call_frame_cfa) {
        return 0;
      }

      return error::Internal("Unsupported operand: $0", magic_enum::enum_name(code));
    }
  }
  return error::Internal("Could not find argument.");
}

}  // namespace dwarf_tools
}  // namespace stirling
}  // namespace pl
