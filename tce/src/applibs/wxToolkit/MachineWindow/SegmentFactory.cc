/**
 * @file SegmentFactory.cc
 *
 * Definition of SegmentFactory class.
 *
 * @author Ari Mets�halme 2003 (ari.metsahalme@tut.fi)
 * @note rating: yellow
 * @note reviewed Jul 14 2004 by jm, ll, jn, am
 */

#include <vector>

#include "Segment.hh"
#include "SegmentFigure.hh"
#include "SegmentFactory.hh"
#include "EditPart.hh"
#include "SegmentFactory.hh"
#include "SocketFactory.hh"
#include "SocketBusConnFactory.hh"
#include "EditPolicyFactory.hh"

using std::vector;
using namespace TTAMachine;

/**
 * The Constructor.
 */
SegmentFactory::SegmentFactory(EditPolicyFactory& editPolicyFactory):
    EditPartFactory(editPolicyFactory) {

    registerFactory(new SocketFactory(editPolicyFactory));
}

/**
 * The Destructor.
 */
SegmentFactory::~SegmentFactory() {
}

/**
 * Returns an EditPart corresponding to a segment.
 *
 * @param component Segment of which to create the EditPart.
 * @return NULL if the parameter is not an instance of the
 *         Segment class.
 */
EditPart*
SegmentFactory::createEditPart(MachinePart* component) {

    Segment* segment = dynamic_cast<Segment*>(component);

    if (segment != NULL) {
	EditPart* segmentEditPart = new EditPart();
	segmentEditPart->setModel(segment);

	SegmentFigure* fig = new SegmentFigure();
	segmentEditPart->setFigure(fig);

	SocketBusConnFactory connFactory;

	for (int j = 0; j < segment->connectionCount(); j++) {
	    vector<Factory*>::const_iterator i = factories_.begin();
	    for (i = factories_.begin(); i != factories_.end(); i++) {
		EditPart* socketEditPart =
		    (*i)->createEditPart((MachinePart*)segment->connection(j));
		if (socketEditPart != NULL) {
		    socketEditPart->addChild(
			connFactory.createConnection(
			    socketEditPart, segmentEditPart));
		    break;
		}
	    }
	}

	EditPolicy* editPolicy = editPolicyFactory_.createSegmentEditPolicy();
	if (editPolicy != NULL) {
	    segmentEditPart->installEditPolicy(editPolicy);
	}

	return segmentEditPart;

    } else {
	return NULL;
    } 
}
