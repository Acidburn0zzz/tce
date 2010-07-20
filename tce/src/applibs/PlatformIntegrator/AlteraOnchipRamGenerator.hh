/*
    Copyright (c) 2002-2010 Tampere University of Technology.

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
 * @file AlteraOnchipRamGenerator.hh
 *
 * Declaration of AlteraOnchipRamGenerator class.
 *
 * @author Otto Esko 2010 (otto.esko-no.spam-tut.fi)
 * @note rating: red
 */

#ifndef TTA_ALTERA_ONCHIP_RAM_GENERATOR_HH
#define TTA_ALTERA_ONCHIP_RAM_GENERATOR_HH

#include <iostream>
#include <string>
#include <vector>
#include "AlteraMegawizMemGenerator.hh"
#include "PlatformIntegrator.hh"

class AlteraOnchipRamGenerator : public AlteraMegawizMemGenerator {
public:

    AlteraOnchipRamGenerator(
        int memMauWidth,
        int widthInMaus,
        int addrWidth,
        std::string initFile,
        const PlatformIntegrator* integrator,
        std::ostream& warningStream,
        std::ostream& errorStream);
    
    virtual ~AlteraOnchipRamGenerator();
    
    virtual bool generatesComponentHdlFile() const;

    virtual std::vector<std::string>
    generateComponentFile(std::string outputPath);

protected:

    virtual std::string createMemParameters() const;

    virtual std::string moduleName() const;
    
    virtual std::string instanceName() const;

};

#endif
