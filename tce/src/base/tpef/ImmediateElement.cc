/**
 * @file ImmediateElement.cc
 *
 * Non-inline definitions of ImmediateElement class.
 *
 * @author Jussi Nyk�nen 2003 (nykanen@cs.tut.fi)
 * @author Mikael Lepist� 2003 (tmlepist@cs.tut.fi)
 * @note reviewed 21 October 2003 by ml, jn, rl, pj
 *
 * @note rating: yellow
 */

#include "ImmediateElement.hh"

namespace TPEF {

/**
 * Constructor.
 */
ImmediateElement::ImmediateElement() :
    InstructionElement(false), destinationUnit_(0), destinationIndex_(0) {
}

/**
 * Destructor.
 */
ImmediateElement::~ImmediateElement() {
}

}
