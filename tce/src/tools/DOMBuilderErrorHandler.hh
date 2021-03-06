/*
    Copyright (c) 2002-2009 Tampere University.

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
 * @file DOMBuilderErrorHandler.hh
 *
 * Declaration of DOMBuilderErrorHandler class.
 *
 * @author Lasse Laasonen 2004 (lasse.laasonen-no.spam-tut.fi)
 */

#ifndef TTA_DOM_BUILDER_ERROR_HANDLER_HH
#define TTA_DOM_BUILDER_ERROR_HANDLER_HH

#include <string>

#include <xercesc/dom/DOMErrorHandler.hpp>
#include <xercesc/dom/DOMError.hpp>

#if _XERCES_VERSION >= 20200
XERCES_CPP_NAMESPACE_USE
#endif

/**
 * Error handler used when validating XML files by XMLSerializer.
 */
class DOMBuilderErrorHandler : public DOMErrorHandler {
public:
    DOMBuilderErrorHandler();
    virtual ~DOMBuilderErrorHandler();

    bool handleError(const DOMError& domError);
    int errorCount() const;
    std::string errorLog() const;

private:
    /// Number of errors handled.
    int errorCount_;
    /// Error log.
    std::string errorLog_;
};

#endif
