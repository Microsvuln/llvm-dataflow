//===- test.cpp - 				    ---------------===//
//
// Sample pass to understand basic block behavior
// 
// 		
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "AvailableExpr"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/User.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Format.h"

#include <llvm/ADT/StringRef.h>
#include "llvm/Support/CFG.h"
#include "AvailableExprAnalysis.h"

#include <stdio.h>

using namespace llvm;
using namespace std;

namespace {
	struct AvailableExprPass : public FunctionPass {
		static char ID; // Pass identification, replacement for typeid
		vector<BasicBlock*> blockOrder; // for use in remembering order of traversed blocks

		map<StringRef, vector<AvailableExprAnalysis *> > blockInstAnalysis; //for use propagating dataflow facts

		AvailableExprPass() : FunctionPass(ID) {}

		virtual bool runOnFunction(Function &F) {
            map<StringRef, BasicBlock*> visitedBlocks; //for use in detecting loop
			errs() << "BEGIN FUNCTION: ";
			errs().write_escaped(F.getName()) << "\n";
			for (Function::iterator B = F.begin(); B != F.end(); ++B) { 
				errs() << "Begin block with name: ";
				errs().write_escaped(B->getName()) << "\n";
				
				// check predecessors to perform merge
				vector< Lattice<ExpressionContainer>* > predecessorEdges;
				errs() << "\tPredecessors:\n";
				for (pred_iterator PI = pred_begin(B); PI != pred_end(B); ++PI) {
  					BasicBlock *pred = *PI;
					errs() << "\t\t";
					errs().write_escaped(pred->getName());
					errs() << "\n";
					// get final instruction's outgoing edge from each predecessor block
					if (!blockInstAnalysis[pred->getName()].empty()) {// some predecessors have not been visited (a loop edge from a future block)
						predecessorEdges.push_back(blockInstAnalysis[pred->getName()].back()->getOutgoingEdge());
					}
					else {
						errs() << "\t\t\tNo incoming edge from this, pushing bottom (full set)\n";
						ExpressionContainer empty;
						predecessorEdges.push_back(new Lattice<ExpressionContainer>(false,true,empty)); //mem leak here
					}
				}
				// perform mergings	
				// if no predecessors: incomingEdge will be bottom
				ExpressionContainer empty;		
				Lattice<ExpressionContainer> * incomingEdge = new Lattice<ExpressionContainer>(false,true,empty);
				if (predecessorEdges.size() == 1)
					incomingEdge = predecessorEdges.front();
                else if (predecessorEdges.size() >= 2) {
                    incomingEdge = AvailableExprAnalysis::merge(predecessorEdges[0], predecessorEdges[1]);
                    for (unsigned int i = 2; i < predecessorEdges.size(); i++) {
                        incomingEdge = AvailableExprAnalysis::merge(incomingEdge, predecessorEdges[2]);
                    }
                }

				// record block visit
				visitedBlocks[B->getName()] = &(*B);
				blockOrder.push_back(&(*B));
				
				// collect dataflow facts
				errs() << "\tInstructions:\n";
				for (BasicBlock::iterator I = B->begin(); I != B->end(); ++I) { 
					errs() << "\t\t";
                    AvailableExprAnalysis * analysis = new AvailableExprAnalysis(&(*I), incomingEdge);
					blockInstAnalysis[B->getName()].push_back(analysis);
					analysis->applyFlowFunction();
                   	I->dump();
					analysis->dump();
					incomingEdge = analysis->getOutgoingEdge(); // for next analysis
				}

				// check successors for presence of a loop
				errs() << "\tSucessors:\n";
				for (succ_iterator SI = succ_begin(B); SI != succ_end(B); ++SI) {
					BasicBlock *successor = *SI;
					errs() << "\t\t";
					errs().write_escaped(successor->getName());
					BasicBlock* blockIfVisited = visitedBlocks[successor->getName()];
					if (blockIfVisited) {
						errs() << " !!!VISITED BEFORE!!!\n";
						blockLoop(blockIfVisited, &(*B)); // handle loop
					}
					else
						errs() << "\n";
				}
			}
			return false; // NOTE WE WILL HAVE TO CHANGE THIS
		}

		void blockLoop(BasicBlock *start, BasicBlock *end) {
			unsigned int startIndex;
			unsigned int endIndex;
			for (unsigned int i = 0; i < blockOrder.size(); i++) {
				if (blockOrder[i] == start) 
					startIndex = i;
				if (blockOrder[i] == end)
					endIndex = i;
			}
			map<StringRef, BasicBlock*> visitedBlocks; //for use in detecting nested loop
			while (true) {
				for (unsigned int i = startIndex; i <= endIndex; i++) {
					BasicBlock * currentBlock = blockOrder[i];
					errs() << "Begin !!!LOOP!!! block with name: ";
					errs().write_escaped(currentBlock->getName()) << "\n";
					// check predecessors to perform merge
					vector< Lattice<ExpressionContainer> * > predecessorEdges;
					errs() << "\tPredecessors:\n";
					for (pred_iterator PI = pred_begin(currentBlock); PI != pred_end(currentBlock); ++PI) {
						BasicBlock *pred = *PI;
						errs() << "\t\t";
						errs().write_escaped(pred->getName());
						errs() << "\n";
						// get final instruction's outgoing edge from each predecessor block
						if (!blockInstAnalysis[pred->getName()].empty()) // some predecessors have not been visited (a loop edge from a future block)
							predecessorEdges.push_back(blockInstAnalysis[pred->getName()].back()->getOutgoingEdge());
						else {
							errs() << "\t\t\tNo incoming edge from this, pushing bottom (full set)\n";
							ExpressionContainer empty;
							predecessorEdges.push_back(new Lattice<ExpressionContainer>(false,true,empty)); //mem leak here
						}
					}
					// perform mergings			
					Lattice<ExpressionContainer> * incomingEdge;
					if (predecessorEdges.size() == 1)
						incomingEdge = predecessorEdges.front();
					else if (predecessorEdges.size() >= 2) {
						incomingEdge = AvailableExprAnalysis::merge(predecessorEdges[0], predecessorEdges[1]);
						for (unsigned int i = 2; i < predecessorEdges.size(); i++) {
							incomingEdge = AvailableExprAnalysis::merge(incomingEdge, predecessorEdges[2]);
						}
					}
					// record block visit
					visitedBlocks[currentBlock->getName()] = currentBlock;
					// collect dataflow facts
					errs() << "\tInstructions:\n";
					for (unsigned int j = 0; j < blockInstAnalysis[currentBlock->getName()].size(); j++) { 
						errs() << "\t\t";
						AvailableExprAnalysis * analysis = blockInstAnalysis[currentBlock->getName()][j];
						Lattice<ExpressionContainer> * originalOut = analysis->getOutgoingEdge();
						analysis->setIncomingEdge(incomingEdge);
						analysis->applyFlowFunction();
						analysis->getInstruction()->dump();
						analysis->dump();
						if(AvailableExprAnalysis::equal(originalOut, analysis->getOutgoingEdge())) {
							errs() << "Encountered fixed dataflow fact --- !!!ENDING LOOP!!!\n";
							return; //we are done
						}
						incomingEdge = analysis->getOutgoingEdge(); // for next analysis
					}

					// check successors for presence of a loop
					errs() << "\tSucessors:\n";
					for (succ_iterator SI = succ_begin(currentBlock); SI != succ_end(currentBlock); ++SI) {
						BasicBlock *successor = *SI;
						errs() << "\t\t";
						errs().write_escaped(successor->getName());
						BasicBlock* blockIfVisited = visitedBlocks[successor->getName()];
						if (blockIfVisited) {
							errs() << " !!!VISITED BEFORE!!!\n";
							blockLoop(blockIfVisited, currentBlock); // handle loop
						}
						else
							errs() << "\n";
					}
				}
				visitedBlocks.clear(); //IMPORTANT, otherwise detect nested loops on subsequent loops
			}

		}
	};
}

char AvailableExprPass::ID = 0;
static RegisterPass<AvailableExprPass> X("avail_expr", "AvailableExpr Pass");
