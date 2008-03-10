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
