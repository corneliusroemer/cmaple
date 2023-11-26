// ============================================================================
// gzstream, C++ iostream classes wrapping the zlib compression library.
// Copyright (C) 2001  Deepak Bandyopadhyay, Lutz Kettner
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
// ============================================================================
//
// File          : gzstream.C
// Revision      : $Revision: 1.7 $
// Revision_date : $Date: 2003/01/08 14:41:27 $
// Author(s)     : Deepak Bandyopadhyay, Lutz Kettner
// 
// Standard streambuf implementation following Nicolai Josuttis, "The 
// Standard C++ Library".
// ============================================================================
//
// Modifications (2020) for keeping track of compressed_length
// and compressed_position

#include "gzstream.h"
#include <cstring> // for memcpy
#include <iostream>

#ifdef GZSTREAM_NAMESPACE
namespace GZSTREAM_NAMESPACE {
#endif

// ----------------------------------------------------------------------------
// Internal classes to implement gzstream. See header file for user classes.
// ----------------------------------------------------------------------------

// --------------------------------------
// class gzstreambuf:
// --------------------------------------

auto gzstreambuf::open(const char *name, int open_mode, int compression_level)
    -> gzstreambuf * {
  if (is_open()) {
    return (gzstreambuf *)nullptr;
  }
  mode = open_mode;
  // no append nor read/write mode
  if ((mode & std::ios::ate) || (mode & std::ios::app) ||
      ((mode & std::ios::in) && (mode & std::ios::out))) {
    return (gzstreambuf *)nullptr;
  }
  char fmode[10];
  char *fmodeptr = fmode;
  if (mode & std::ios::in) {
    *fmodeptr++ = 'r';
  } else if (mode & std::ios::out) {
    *fmodeptr++ = 'w';
  }
  *fmodeptr++ = 'b';
  if (mode & GZ_NO_COMPRESSION) {
    *fmodeptr++ = '0'; // no compression
  } else {
    *fmodeptr++ = '1'; // fast compression ratio
  }
  *fmodeptr = '\0';

  // BEGIN - Figure out length of the compressed file
  if (mode & std::ios::in) {
    FILE *fp = fopen(name, "rb");
    if (fp) {
      fseek(fp, 0, SEEK_END);
#if defined(WIN64)
      compressed_length = _ftelli64(fp);
#elif defined(WIN32)
      compressed_length = ftell(fp);
#else
      compressed_length = ftello(fp);
#endif
      fclose(fp);
    }
  }
  // FINISH - Determining compressed_length

  file = gzopen(name, fmode);
  if (file == nullptr) {
    return (gzstreambuf *)nullptr;
  }
  opened = 1;

  if (mode & std::ios::out) {
    gzsetparams(file, compression_level, Z_DEFAULT_STRATEGY);
  }

  return this;
}

auto gzstreambuf::close() -> gzstreambuf * {
  if (is_open()) {
    sync();
    opened = 0;
    if (gzclose(file) == Z_OK) {
      return this;
    }
  }
  return (gzstreambuf *)nullptr;
}

auto gzstreambuf::underflow() -> int { // used for input buffer only
  if (gptr() && (gptr() < egptr())) {
    return *reinterpret_cast<unsigned char *>(gptr());
  }

  if (!(mode & std::ios::in) || !opened) {
    return EOF;
  }
  // Josuttis' implementation of inbuf
  long n_putback = gptr() - eback();
  if (n_putback > 4) {
    n_putback = 4;
  }
  memcpy(buffer + (4 - n_putback), gptr() - n_putback, n_putback);

  int num = gzread(file, buffer + 4, bufferSize - 4);
  if (num <= 0) { // ERROR or EOF
    return EOF;
  }

  compressed_position = gzoffset(file);

  // reset buffer pointers
  setg(buffer + (4 - n_putback), // beginning of putback area
       buffer + 4,               // read position
       buffer + 4 + num);        // end of buffer

  // return next character
  return *reinterpret_cast<unsigned char *>(gptr());
}

auto gzstreambuf::flush_buffer() -> int {
  // Separate the writing of the buffer from overflow() and
  // sync() operation.
  int w = static_cast<int>(pptr() - pbase());
  long wDone = gzwrite(file, pbase(), w);
  if (wDone != w) {
    return EOF;
  }
  pbump(-w);
  return w;
}

auto gzstreambuf::overflow(int c) -> int { // used for output buffer only
  if (!(mode & std::ios::out) || !opened) {
    return EOF;
  }
  if (c != EOF) {
    *pptr() = c;
    pbump(1);
  }
  if (flush_buffer() == EOF) {
    return EOF;
  }
  return c;
}

auto gzstreambuf::sync() -> int {
  // Changed to use flush_buffer() instead of overflow( EOF)
  // which caused improper behavior with std::endl and flush(),
  // bug reported by Vincent Ricard.
  if (pptr() && pptr() > pbase()) {
    if (flush_buffer() == EOF) {
      return -1;
    }
  }
  return 0;
}

auto gzstreambuf::getCompressedLength() -> size_t { return compressed_length; }

auto gzstreambuf::getCompressedPosition() -> size_t {
  return compressed_position;
}

// --------------------------------------
// class gzstreambase:
// --------------------------------------

gzstreambase::gzstreambase( const char* name, int mode) {
    init( &buf);
    open( name, mode);
}

gzstreambase::~gzstreambase() {
    buf.close();
}

void gzstreambase::open( const char* name, int open_mode) {
  if (!buf.open(name, open_mode)) {
    clear(rdstate() | std::ios::badbit);
  }
}

void gzstreambase::close() {
  if (buf.is_open()) {
    if (!buf.close()) {
      clear(rdstate() | std::ios::badbit);
    }
  }
}

auto gzstreambase::get_raw_bytes() -> z_off_t { return gztell(buf.file); }

#ifdef GZSTREAM_NAMESPACE
} // namespace GZSTREAM_NAMESPACE
#endif

// ============================================================================
// EOF //
