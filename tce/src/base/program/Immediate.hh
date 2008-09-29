/*
    Copyright 2002-2008 Tampere University of Technology.  All Rights
    Reserved.

    This file is part of TTA-Based Codesign Environment (TCE).

    TCE is free software; you can redistribute it and/or modify it under the
    terms of the GNU General Public License version 2 as published by the Free
    Software Foundation.

    TCE is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
    details.

    You should have received a copy of the GNU General Public License along
    with TCE; if not, write to the Free Software Foundation, Inc., 51 Franklin
    St, Fifth Floor, Boston, MA  02110-1301  USA

    As a special exception, you may use this file as part of a free software
    library without restriction.  Specifically, if other files instantiate
    templates or use macros or inline functions from this file, or you compile
    this file and link it with other files to produce an executable, this
    file does not by itself cause the resulting executable to be covered by
    the GNU General Public License.  This exception does not however
    invalidate any other reasons why the executable file might be covered by
    the GNU General Public License.
*/
/**
 * @file Immediate.hh
 *
 * Declaration of Immediate class.
 *
 * @author Ari Mets�halme 2005 (ari.metsahalme-no.spam-tut.fi)
 * @note rating: red
 */

#ifndef TTA_IMMEDIATE_HH
#define TTA_IMMEDIATE_HH

#include "SimValue.hh"
#include "Exception.hh"

namespace TTAMachine {
    class InstructionTemplate;
}

namespace TTAProgram {

class Terminal;
class TerminalImmediate;

/**
 * Represents the long immediate value and its transport into a
 * dedicated register
 */
class Immediate {
public:
    Immediate(
        TerminalImmediate* value, Terminal* dst)
        throw (IllegalParameters);

    virtual ~Immediate();

    const Terminal& destination() const;

    TerminalImmediate& value() const;
    void setValue(TerminalImmediate* value);

    Immediate* copy() const;

private:
    /// Copying not allowed.
    Immediate(const Immediate&);
    /// Assignment not allowed.
    Immediate& operator=(const Immediate&);

    /// Value of the immediate.
    // SimValue value_;
    TerminalImmediate* value_;

    /// The destination register.
    Terminal* dst_;
};

}

#endif
