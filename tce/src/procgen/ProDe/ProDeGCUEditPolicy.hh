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
 * @file ProDeGCUEditPolicy.hh
 *
 * Declaration of ProDeGCUEditPolicy class.
 *
 * @author Ari Mets�halme 2003 (ari.metsahalme@tut.fi)
 * @note rating: yellow
 * @note reviewed Jul 20 2004 by vpj, jn, am
 */

#ifndef TTA_PRODE_GCU_EDIT_POLICY_HH
#define TTA_PRODE_GCU_EDIT_POLICY_HH


#include "EditPolicy.hh"

class Request;
class ComponentCommand;

/**
 * Determines how a GCU EditPart acts when a Request is performed
 * on it.
 *
 * Converts a given Request to a Command if the EditPolicy supports
 * the Request.
 */
class ProDeGCUEditPolicy : public EditPolicy {
public:
    ProDeGCUEditPolicy();
    virtual ~ProDeGCUEditPolicy();

    virtual ComponentCommand* getCommand(Request* request);
    virtual bool canHandle(Request* request) const;

private:
    /// Assignment not allowed.
    ProDeGCUEditPolicy& operator=(ProDeGCUEditPolicy& old);
    /// Copying not allowed.
    ProDeGCUEditPolicy(ProDeGCUEditPolicy& old);
};

#endif
