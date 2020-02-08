/* DWARF 2 debugging format support for GDB.

   Copyright (C) 1994-2020 Free Software Foundation, Inc.

   Adapted by Gary Funck (gary@intrepid.com), Intrepid Technology,
   Inc.  with support from Florida State University (under contract
   with the Ada Joint Program Office), and Silicon Graphics, Inc.
   Initial contribution by Brent Benson, Harris Computer Systems, Inc.,
   based on Fred Fish's (Cygnus Support) implementation of DWARF 1
   support.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include "defs.h"
#include "dwarf2/comp-unit.h"
#include "dwarf2/leb.h"
#include "dwarf2/read.h"
#include "dwarf2/section.h"

/* Convert a unit type to corresponding DW_UT name.  */

static const char *
dwarf_unit_type_name (int unit_type)
{
  switch (unit_type)
    {
      case 0x01:
	return "DW_UT_compile (0x01)";
      case 0x02:
	return "DW_UT_type (0x02)";
      case 0x03:
	return "DW_UT_partial (0x03)";
      case 0x04:
	return "DW_UT_skeleton (0x04)";
      case 0x05:
	return "DW_UT_split_compile (0x05)";
      case 0x06:
	return "DW_UT_split_type (0x06)";
      case 0x80:
	return "DW_UT_lo_user (0x80)";
      case 0xff:
	return "DW_UT_hi_user (0xff)";
      default:
	return nullptr;
    }
}

/* See comp-unit.h.  */

const gdb_byte *
read_comp_unit_head (struct comp_unit_head *cu_header,
		     const gdb_byte *info_ptr,
		     struct dwarf2_section_info *section,
		     rcuh_kind section_kind)
{
  int signed_addr;
  unsigned int bytes_read;
  const char *filename = section->get_file_name ();
  bfd *abfd = section->get_bfd_owner ();

  cu_header->length = read_initial_length (abfd, info_ptr, &bytes_read);
  cu_header->initial_length_size = bytes_read;
  cu_header->offset_size = (bytes_read == 4) ? 4 : 8;
  info_ptr += bytes_read;
  cu_header->version = read_2_bytes (abfd, info_ptr);
  if (cu_header->version < 2 || cu_header->version > 5)
    error (_("Dwarf Error: wrong version in compilation unit header "
	   "(is %d, should be 2, 3, 4 or 5) [in module %s]"),
	   cu_header->version, filename);
  info_ptr += 2;
  if (cu_header->version < 5)
    switch (section_kind)
      {
      case rcuh_kind::COMPILE:
	cu_header->unit_type = DW_UT_compile;
	break;
      case rcuh_kind::TYPE:
	cu_header->unit_type = DW_UT_type;
	break;
      default:
	internal_error (__FILE__, __LINE__,
			_("read_comp_unit_head: invalid section_kind"));
      }
  else
    {
      cu_header->unit_type = static_cast<enum dwarf_unit_type>
						 (read_1_byte (abfd, info_ptr));
      info_ptr += 1;
      switch (cu_header->unit_type)
	{
	case DW_UT_compile:
	case DW_UT_partial:
	case DW_UT_skeleton:
	case DW_UT_split_compile:
	  if (section_kind != rcuh_kind::COMPILE)
	    error (_("Dwarf Error: wrong unit_type in compilation unit header "
		   "(is %s, should be %s) [in module %s]"),
		   dwarf_unit_type_name (cu_header->unit_type),
		   dwarf_unit_type_name (DW_UT_type), filename);
	  break;
	case DW_UT_type:
	case DW_UT_split_type:
	  section_kind = rcuh_kind::TYPE;
	  break;
	default:
	  error (_("Dwarf Error: wrong unit_type in compilation unit header "
		 "(is %#04x, should be one of: %s, %s, %s, %s or %s) "
		 "[in module %s]"), cu_header->unit_type,
		 dwarf_unit_type_name (DW_UT_compile),
		 dwarf_unit_type_name (DW_UT_skeleton),
		 dwarf_unit_type_name (DW_UT_split_compile),
		 dwarf_unit_type_name (DW_UT_type),
		 dwarf_unit_type_name (DW_UT_split_type), filename);
	}

      cu_header->addr_size = read_1_byte (abfd, info_ptr);
      info_ptr += 1;
    }
  cu_header->abbrev_sect_off = (sect_offset) read_offset (abfd, info_ptr,
							  cu_header,
							  &bytes_read);
  info_ptr += bytes_read;
  if (cu_header->version < 5)
    {
      cu_header->addr_size = read_1_byte (abfd, info_ptr);
      info_ptr += 1;
    }
  signed_addr = bfd_get_sign_extend_vma (abfd);
  if (signed_addr < 0)
    internal_error (__FILE__, __LINE__,
		    _("read_comp_unit_head: dwarf from non elf file"));
  cu_header->signed_addr_p = signed_addr;

  bool header_has_signature = section_kind == rcuh_kind::TYPE
    || cu_header->unit_type == DW_UT_skeleton
    || cu_header->unit_type == DW_UT_split_compile;

  if (header_has_signature)
    {
      cu_header->signature = read_8_bytes (abfd, info_ptr);
      info_ptr += 8;
    }

  if (section_kind == rcuh_kind::TYPE)
    {
      LONGEST type_offset;
      type_offset = read_offset (abfd, info_ptr, cu_header, &bytes_read);
      info_ptr += bytes_read;
      cu_header->type_cu_offset_in_tu = (cu_offset) type_offset;
      if (to_underlying (cu_header->type_cu_offset_in_tu) != type_offset)
	error (_("Dwarf Error: Too big type_offset in compilation unit "
	       "header (is %s) [in module %s]"), plongest (type_offset),
	       filename);
    }

  return info_ptr;
}

/* Subroutine of read_and_check_comp_unit_head and
   read_and_check_type_unit_head to simplify them.
   Perform various error checking on the header.  */

static void
error_check_comp_unit_head (struct dwarf2_per_objfile *dwarf2_per_objfile,
			    struct comp_unit_head *header,
			    struct dwarf2_section_info *section,
			    struct dwarf2_section_info *abbrev_section)
{
  const char *filename = section->get_file_name ();

  if (to_underlying (header->abbrev_sect_off)
      >= abbrev_section->get_size (dwarf2_per_objfile->objfile))
    error (_("Dwarf Error: bad offset (%s) in compilation unit header "
	   "(offset %s + 6) [in module %s]"),
	   sect_offset_str (header->abbrev_sect_off),
	   sect_offset_str (header->sect_off),
	   filename);

  /* Cast to ULONGEST to use 64-bit arithmetic when possible to
     avoid potential 32-bit overflow.  */
  if (((ULONGEST) header->sect_off + header->get_length ())
      > section->size)
    error (_("Dwarf Error: bad length (0x%x) in compilation unit header "
	   "(offset %s + 0) [in module %s]"),
	   header->length, sect_offset_str (header->sect_off),
	   filename);
}

/* See comp-unit.h.  */

const gdb_byte *
read_and_check_comp_unit_head (struct dwarf2_per_objfile *dwarf2_per_objfile,
			       struct comp_unit_head *header,
			       struct dwarf2_section_info *section,
			       struct dwarf2_section_info *abbrev_section,
			       const gdb_byte *info_ptr,
			       rcuh_kind section_kind)
{
  const gdb_byte *beg_of_comp_unit = info_ptr;

  header->sect_off = (sect_offset) (beg_of_comp_unit - section->buffer);

  info_ptr = read_comp_unit_head (header, info_ptr, section, section_kind);

  header->first_die_cu_offset = (cu_offset) (info_ptr - beg_of_comp_unit);

  error_check_comp_unit_head (dwarf2_per_objfile, header, section,
			      abbrev_section);

  return info_ptr;
}

/* See comp-unit.h.  */

LONGEST
read_offset (bfd *abfd, const gdb_byte *buf,
	     const struct comp_unit_head *cu_header,
             unsigned int *bytes_read)
{
  LONGEST offset = read_offset (abfd, buf, cu_header->offset_size);

  *bytes_read = cu_header->offset_size;
  return offset;
}