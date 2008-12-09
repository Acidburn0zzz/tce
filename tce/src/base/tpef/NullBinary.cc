/*
    Copyright (c) 2002-2009 Tampere University of Technology.

    This file is part of TTA-Based Codesign Environment (TCE).

    Permission is hereby granted, free of charge, to any person obtaining a
    copy of this software and associated documentation files (the "Software"),
    to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense,
    and/or sell copies of the Software, and to permit persons to whom the
    Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
    DEALINGS IN THE SOFTWARE.
 */
/**
 * @file NullBinary.cc
 *
 * Implementation of NullBinary class.
 *
 * @author Jari M�ntyneva 2006 (jari.mantyneva-no.spam-tut.fi)
 * @note rating: yellow
 */

#include "NullBinary.hh"

namespace TPEF {

/////////////////////////////////////////////////////////////////////////////
// NullBinary
/////////////////////////////////////////////////////////////////////////////

NullBinary NullBinary::instance_;

/**
 * The constructor.
 */
NullBinary::NullBinary() : Binary() {
}

/**
 * The destructor.
 */
NullBinary::~NullBinary() {
}

/**
 * Returns an instance of NullBinary class (singleton).
 *
 * @return Singleton instance of NullBinary class.
 */
NullBinary&
NullBinary::instance() {
    return instance_;
}

}
