//
// Created by Qiu Haoze on 24/11/19.
//
#ifndef SOLIDITY_ITASK_H
#define SOLIDITY_ITASK_H

#include <libsolidity/ast/AST.h>

namespace dev
{
namespace solidity
{
namespace verifier
{

class ITask {
public:
    ITask(const SourceUnit *_ast, const ASTNode *_target) : m_ast(_ast), m_target(_target) {};
    static const std::string taskName;

    virtual void execute() { return; };

    static ITask *Create(const SourceUnit *_ast, const ASTNode *_target);

    [[nodiscard]] const SourceUnit *getAst() const {
        return m_ast;
    }

    [[nodiscard]] const ASTNode *getTarget() const {
        return m_target;
    }

private:
    const SourceUnit *m_ast;
    const ASTNode *m_target;
};

}
}
}
#endif //SOLIDITY_ITASK_H
