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
 * @file GlobalLock.cc
 *
 * Definition of GlobalLock class.
 *
 * @author Jussi Nyk�nen 2005 (nykanen-no.spam-cs.tut.fi)
 * @note rating: red
 */

#include "GlobalLock.hh"

/**
 * Constructor.
 */
GlobalLock::GlobalLock() : isLocked_(false) {
}

/**
 * Destructor.
 */
GlobalLock::~GlobalLock() {
}

/**
 * Raises the lock signal.
 */
void
GlobalLock::lock() {
    isLocked_ = true;
}

/**
 * Lowers the clock signal.
 */
void
GlobalLock::unlock() {
    isLocked_ = false;
}

/**
 * Returns true if lock signal is raised, false otherwise.
 *
 * @return True if lock signal is raised, false otherwise.
 */
bool
GlobalLock::isLocked() const {
    return isLocked_;
}
