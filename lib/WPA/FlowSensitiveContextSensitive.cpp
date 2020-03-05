//===- FlowSensitiveContextSensitive.cpp -- flow-sensitive context-sensitive PTA ------//

/*
 * FlowSensitiveContextSensitive.cpp
 *
 *  Created on: Mar 05, 2020
 *      Author: Mohamad Barbar
 */

#include "WPA/FlowSensitiveContextSensitive.h"

/// Maximum value for k (calling context depth).
static llvm::cl::opt<unsigned> kLimit("k-limit",  llvm::cl::init(20),
        llvm::cl::desc("Maximum calling context depth (default = 20)"));

FlowSensitiveContextSensitive::FlowSensitiveContextSensitive(PTATY type) : FlowSensitive(type) { }

void FlowSensitiveContextSensitive::analyze(SVFModule svfModule) {
    FlowSensitive::analyze(svfModule);
}

void FlowSensitiveContextSensitive::initialize(SVFModule svfModule) {
    FlowSensitive::initialize(svfModule);
}

void FlowSensitiveContextSensitive::finalize(void) {
    FlowSensitive::finalize();
}
