/**
 * @file SafePointable.hh
 *
 * Declaration of SafePointable interface class.
 *
 * @author Pekka J��skel�inen 2003 (pjaaskel@cs.tut.fi)
 * @author Mikael Lepist� 2005 (tmlepist@cs.tut.fi)
 * @note Reviewed 29 Aug 2003 by Risto M�kinen, Mikael Lepist�, Andrea Cilio,
 * Tommi Rantanen
 *
 * @note rating: yellow
 */

#ifndef TTA_SAFEPOINTABLE_HH
#define TTA_SAFEPOINTABLE_HH

#include "Application.hh" // debug

namespace TPEF {

/**
 * All classes that implement the SafePointable interface can be used
 * in reference manager.
 */
class SafePointable {
public:
    virtual ~SafePointable();
protected:
    // this is an 'interface' class so we don't want direct instances of it
    SafePointable();

};
}
#endif
