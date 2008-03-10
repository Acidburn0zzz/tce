/**
 * @file RFFactory.cc
 *
 * Definition of RFFactory class.
 *
 * @author Ari Mets�halme 2003 (ari.metsahalme@tut.fi)
 * @note rating: yellow
 * @note reviewed Jul 14 2004 by jm, ll, jn, am
 */

#include <string>
#include <vector>

#include "RFFactory.hh"
#include "UnitPortFactory.hh"
#include "RegisterFile.hh"
#include "EditPart.hh"
#include "UnitFigure.hh"
#include "WxConversion.hh"
#include "FUPort.hh"
#include "EditPolicyFactory.hh"
#include "UnboundedRegisterFile.hh"

using std::vector;
using namespace TTAMachine;

/**
 * The Constructor.
 */
RFFactory::RFFactory(EditPolicyFactory& editPolicyFactory):
    EditPartFactory(editPolicyFactory) {

    registerFactory(new UnitPortFactory(editPolicyFactory));
}

/**
 * The Destructor.
 */
RFFactory::~RFFactory() {
}

/**
 * Returns an EditPart corresponding to a register file.
 *
 * @param component Register file of which to create the EditPart.
 * @return NULL if the parameter is not an instance of the
 *         RegisterFile class.
 */
EditPart*
RFFactory::createEditPart(MachinePart* component) {

    RegisterFile* rf = dynamic_cast<RegisterFile*>(component);

    if (rf != NULL) {
	EditPart* rfEditPart = new EditPart();
	rfEditPart->setModel(rf);

	UnitFigure* fig = new UnitFigure();
	wxString name = WxConversion::toWxString(rf->name().c_str());
	name.Prepend(_T("RF: "));
	fig->setName(name);
	rfEditPart->setFigure(fig);

	for (int i = 0; i < rf->portCount(); i++) {
	    vector<Factory*>::const_iterator iter;
	    for (iter = factories_.begin(); iter != factories_.end(); iter++) {
		EditPart* portEditPart =
		    (*iter)->createEditPart(rf->port(i));
		if (portEditPart != NULL) {

		    EditPolicy* editPolicy =
			editPolicyFactory_.createRFPortEditPolicy();

		    if (editPolicy != NULL) {
			portEditPart->installEditPolicy(editPolicy);
		    }

		    rfEditPart->addChild(portEditPart);
		}
	    }
	}

        wxString info;
        if (dynamic_cast<UnboundedRegisterFile*>(rf) != NULL) {
            info.Append(_T("?"));
        } else {
            info.Append(WxConversion::toWxString(rf->numberOfRegisters()));
        }
        info.Append(_T("x"));
        info.Append(WxConversion::toWxString(rf->width()));

        fig->setInfo(info);

	rfEditPart->setSelectable(true);

	EditPolicy* editPolicy = editPolicyFactory_.createRFEditPolicy();
	if (editPolicy != NULL) {
	    rfEditPart->installEditPolicy(editPolicy);
	}

	return rfEditPart;

    } else {
	return NULL;
    } 
}
