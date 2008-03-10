/**
 * @file ASpaceElement.cc
 *
 * Non-inline definitions of ASpaceElement class.
 *
 * @author Mikael Lepist� 2003 (tmlepist@cs.tut.fi)
 *
 * @note rating: yellow
 */

#include "ASpaceElement.hh"

namespace TPEF {

/**
 * Constructs empty element.
 */
ASpaceElement::ASpaceElement() :
  mau_(0), align_(0), wSize_(0),
  name_(&ReferenceManager::SafePointer::null) {

}

/**
 * Destructor.
 */
ASpaceElement::~ASpaceElement() {
}

}
