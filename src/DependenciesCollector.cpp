//                        Caide C++ inliner
//
// This file is distributed under the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or (at your
// option) any later version. See LICENSE.TXT for details.

#include "DependenciesCollector.h"
#include "SourceInfo.h"
#include "util.h"

//#define CAIDE_DEBUG_MODE
#include "caide_debug.h"


#include <clang/AST/ASTContext.h>
#include <clang/AST/RawCommentList.h>
#include <clang/Basic/SourceManager.h>

#include <string>


using namespace clang;
using std::set;

namespace caide {
namespace internal {

bool DependenciesCollector::TraverseDecl(Decl* decl) {
    declStack.push(decl);
    bool ret = RecursiveASTVisitor<DependenciesCollector>::TraverseDecl(decl);
    declStack.pop();
    return ret;
}


Decl* DependenciesCollector::getCurrentDecl() const {
    return declStack.empty() ? nullptr : declStack.top();
}

FunctionDecl* DependenciesCollector::getCurrentFunction(Decl* decl) const {
    DeclContext* declCtx = decl->getLexicalDeclContext();
    return dyn_cast_or_null<FunctionDecl>(declCtx);
}

Decl* DependenciesCollector::getParentDecl(Decl* decl) const {
    return dyn_cast_or_null<Decl>(decl->getLexicalDeclContext());
}

void DependenciesCollector::insertReference(Decl* from, Decl* to) {
    if (!from || !to)
        return;
    from = from->getCanonicalDecl();
    to = to->getCanonicalDecl();
    srcInfo.uses[from].insert(to);
    dbg("Reference   FROM    " << from->getDeclKindName() << " " << from
        << "<" << toString(sourceManager, from).substr(0, 30) << ">"
        << toString(sourceManager, from->getSourceRange())
        << "     TO     " << to->getDeclKindName() << " " << to
        << "<" << toString(sourceManager, to).substr(0, 30) << ">"
        << toString(sourceManager, to->getSourceRange())
        << std::endl);
}

void DependenciesCollector::insertReferenceToType(Decl* from, const Type* to,
        set<const Type*>& seen)
{
    if (!to)
        return;

    if (!seen.insert(to).second)
        return;

    if (const ElaboratedType* elaboratedType = dyn_cast<ElaboratedType>(to)) {
        insertReferenceToType(from, elaboratedType->getNamedType(), seen);
        return;
    }

    if (const ParenType* parenType = dyn_cast<ParenType>(to))
        insertReferenceToType(from, parenType->getInnerType(), seen);

    insertReference(from, to->getAsTagDecl());

    if (const ArrayType* arrayType = dyn_cast<ArrayType>(to))
        insertReferenceToType(from, arrayType->getElementType(), seen);

    if (const PointerType* pointerType = dyn_cast<PointerType>(to))
        insertReferenceToType(from, pointerType->getPointeeType(), seen);

    if (const ReferenceType* refType = dyn_cast<ReferenceType>(to))
        insertReferenceToType(from, refType->getPointeeType(), seen);

    if (const TypedefType* typedefType = dyn_cast<TypedefType>(to))
        insertReference(from, typedefType->getDecl());

    if (const CXXRecordDecl* recordDecl = to->getAsCXXRecordDecl()) {
        if ((recordDecl = recordDecl->getDefinition())) {
            bool isTemplated = recordDecl->getDescribedClassTemplate() != 0;
            TemplateSpecializationKind specKind = recordDecl->getTemplateSpecializationKind();
            if (isTemplated && (specKind == TSK_ImplicitInstantiation || specKind == TSK_Undeclared)) {}
            else {
                for (const CXXBaseSpecifier* base = recordDecl->bases_begin();
                     base != recordDecl->bases_end(); ++base)
                {
                    insertReferenceToType(from, base->getType(), seen);
                }
            }
        }
    }

    if (const TemplateSpecializationType* tempSpecType =
            dyn_cast<TemplateSpecializationType>(to))
    {
        if (TemplateDecl* tempDecl = tempSpecType->getTemplateName().getAsTemplateDecl())
            insertReference(from, tempDecl);
        for (unsigned i = 0; i < tempSpecType->getNumArgs(); ++i) {
            const TemplateArgument& arg = tempSpecType->getArg(i);
            if (arg.getKind() == TemplateArgument::Type)
                insertReferenceToType(from, arg.getAsType(), seen);
        }
    }
}

void DependenciesCollector::insertReferenceToType(Decl* from, QualType to,
        set<const Type*>& seen)
{
    insertReferenceToType(from, to.getTypePtrOrNull(), seen);
}

void DependenciesCollector::insertReferenceToType(Decl* from, QualType to)
{
    set<const Type*> seen;
    insertReferenceToType(from, to, seen);
}

void DependenciesCollector::insertReferenceToType(Decl* from, const Type* to)
{
    set<const Type*> seen;
    insertReferenceToType(from, to, seen);
}

void DependenciesCollector::insertReferenceToType(Decl* from, const TypeSourceInfo* typeSourceInfo) {
    if (typeSourceInfo)
        insertReferenceToType(from, typeSourceInfo->getType());
}

DependenciesCollector::DependenciesCollector(SourceManager& srcMgr, SourceInfo& srcInfo_)
    : sourceManager(srcMgr)
    , srcInfo(srcInfo_)
{
}

bool DependenciesCollector::shouldVisitImplicitCode() const { return true; }
bool DependenciesCollector::shouldVisitTemplateInstantiations() const { return true; }
bool DependenciesCollector::shouldWalkTypesOfTypeLocs() const { return true; }

bool DependenciesCollector::VisitDecl(Decl* decl) {
    dbg("DECL " << decl->getDeclKindName() << " " << decl
        << "<" << toString(sourceManager, decl).substr(0, 30) << ">"
        << toString(sourceManager, getExpansionRange(sourceManager, decl))
        << std::endl);

    // Mark dependence on enclosing (semantic) class/namespace.
    Decl* ctx = dyn_cast_or_null<Decl>(decl->getDeclContext());
    if (ctx && !isa<FunctionDecl>(ctx))
        insertReference(decl, ctx);

    if (!sourceManager.isInMainFile(decl->getLocStart()))
        return true;

    RawComment* comment = decl->getASTContext().getRawCommentForDeclNoCache(decl);
    if (!comment)
        return true;

    bool invalid = false;
    const char* beg = sourceManager.getCharacterData(comment->getLocStart(), &invalid);
    if (!beg || invalid)
        return true;

    const char* end =
        sourceManager.getCharacterData(comment->getLocEnd(), &invalid);
    if (!end || invalid)
        return true;

    static const std::string caideKeepComment = "caide keep";
    StringRef haystack(beg, end - beg + 1);
    StringRef needle(caideKeepComment);
    //std::cerr << toString(sourceManager, decl) << ": " << haystack.str() << std::endl;
    if (haystack.find(needle) != StringRef::npos)
        srcInfo.declsToKeep.insert(decl);

    return true;
}

bool DependenciesCollector::VisitCallExpr(CallExpr* callExpr) {
    dbg(CAIDE_FUNC);
    Expr* callee = callExpr->getCallee();
    Decl* calleeDecl = callExpr->getCalleeDecl();

    if (!callee || !calleeDecl || isa<UnresolvedMemberExpr>(callee) || isa<CXXDependentScopeMemberExpr>(callee))
        return true;

    insertReference(getCurrentDecl(), calleeDecl);
    return true;
}

bool DependenciesCollector::VisitCXXConstructExpr(CXXConstructExpr* constructorExpr) {
    dbg(CAIDE_FUNC);
    insertReference(getCurrentDecl(), constructorExpr->getConstructor());
    return true;
}

bool DependenciesCollector::VisitCXXTemporaryObjectExpr(CXXTemporaryObjectExpr* tempExpr) {
    insertReferenceToType(getCurrentDecl(), tempExpr->getTypeSourceInfo());
    return true;
}

bool DependenciesCollector::VisitTemplateTypeParmDecl(TemplateTypeParmDecl* paramDecl) {
    if (paramDecl->hasDefaultArgument())
        insertReferenceToType(getParentDecl(paramDecl), paramDecl->getDefaultArgument());
    return true;
}

bool DependenciesCollector::VisitCXXNewExpr(CXXNewExpr* newExpr) {
    insertReferenceToType(getCurrentDecl(), newExpr->getAllocatedType());
    return true;
}

bool DependenciesCollector::VisitDeclRefExpr(DeclRefExpr* ref) {
    dbg(CAIDE_FUNC);
    Decl* parent = getCurrentDecl();
    insertReference(parent, ref->getDecl());
    NestedNameSpecifier* specifier = ref->getQualifier();
    while (specifier) {
        insertReferenceToType(parent, specifier->getAsType());
        specifier = specifier->getPrefix();
    }
    return true;
}

bool DependenciesCollector::VisitCXXScalarValueInitExpr(CXXScalarValueInitExpr* initExpr) {
    insertReferenceToType(getCurrentDecl(), initExpr->getTypeSourceInfo());
    return true;
}

bool DependenciesCollector::VisitExplicitCastExpr(ExplicitCastExpr* castExpr) {
    insertReferenceToType(getCurrentDecl(), castExpr->getTypeAsWritten());
    return true;
}

bool DependenciesCollector::VisitValueDecl(ValueDecl* valueDecl) {
    dbg(CAIDE_FUNC);
    // Mark any function as depending on its local variables.
    // TODO: detect unused local variables.
    insertReference(getCurrentFunction(valueDecl), valueDecl);

    insertReferenceToType(valueDecl, valueDecl->getType());
    return true;
}

// X->F and X.F
bool DependenciesCollector::VisitMemberExpr(MemberExpr* memberExpr) {
    dbg(CAIDE_FUNC);
    insertReference(getCurrentDecl(), memberExpr->getMemberDecl());
    return true;
}

bool DependenciesCollector::VisitLambdaExpr(LambdaExpr* lambdaExpr) {
    dbg(CAIDE_FUNC);
    insertReference(getCurrentDecl(), lambdaExpr->getCallOperator());
    return true;
}

bool DependenciesCollector::VisitFieldDecl(FieldDecl* field) {
    dbg(CAIDE_FUNC);
    insertReference(field, field->getParent());
    return true;
}

// Includes both typedef and type alias
bool DependenciesCollector::VisitTypedefNameDecl(TypedefNameDecl* typedefDecl) {
    dbg(CAIDE_FUNC);
    insertReferenceToType(typedefDecl, typedefDecl->getUnderlyingType());
    return true;
}

bool DependenciesCollector::VisitTypeAliasDecl(TypeAliasDecl* aliasDecl) {
    dbg(CAIDE_FUNC);
    insertReference(aliasDecl, aliasDecl->getDescribedAliasTemplate());
    return true;
}

bool DependenciesCollector::VisitTypeAliasTemplateDecl(TypeAliasTemplateDecl* aliasTemplateDecl) {
    dbg(CAIDE_FUNC);
    insertReference(aliasTemplateDecl, aliasTemplateDecl->getInstantiatedFromMemberTemplate());
    return true;
}

bool DependenciesCollector::VisitClassTemplateDecl(ClassTemplateDecl* templateDecl) {
    dbg(CAIDE_FUNC);
    insertReference(templateDecl, templateDecl->getTemplatedDecl());
    return true;
}

bool DependenciesCollector::VisitClassTemplateSpecializationDecl(ClassTemplateSpecializationDecl* specDecl) {
    dbg(CAIDE_FUNC);
    // specDecl is canonical (e.g. all typedefs are removed).
    // Add reference to the type that is actually written in the code.
    insertReferenceToType(specDecl, specDecl->getTypeAsWritten());

    llvm::PointerUnion<ClassTemplateDecl*, ClassTemplatePartialSpecializationDecl*>
        instantiatedFrom = specDecl->getSpecializedTemplateOrPartial();

    if (instantiatedFrom.is<ClassTemplateDecl*>())
        insertReference(specDecl, instantiatedFrom.get<ClassTemplateDecl*>());
    else if (instantiatedFrom.is<ClassTemplatePartialSpecializationDecl*>())
        insertReference(specDecl, instantiatedFrom.get<ClassTemplatePartialSpecializationDecl*>());

    return true;
}

/*
Every function template is represented as a FunctionTemplateDecl and a FunctionDecl
(or something derived from FunctionDecl). The former contains template properties
(such as the template parameter lists) while the latter contains the actual description
of the template's contents. FunctionTemplateDecl::getTemplatedDecl() retrieves the
FunctionDecl that describes the function template,
FunctionDecl::getDescribedFunctionTemplate() retrieves the FunctionTemplateDecl
from a FunctionDecl.

We only use FunctionDecl's for dependency tracking.
 */
bool DependenciesCollector::VisitFunctionDecl(FunctionDecl* f) {
    dbg(CAIDE_FUNC);
    if (f->isMain())
        srcInfo.declsToKeep.insert(f);

    if (sourceManager.isInMainFile(f->getLocStart()) && f->isLateTemplateParsed())
        srcInfo.delayedParsedFunctions.push_back(f);

    if (f->getTemplatedKind() == FunctionDecl::TK_FunctionTemplate) {
        // skip non-instantiated template function
        return true;
    }

    FunctionTemplateSpecializationInfo* specInfo = f->getTemplateSpecializationInfo();
    if (specInfo) {
        insertReference(f, specInfo->getTemplate()->getTemplatedDecl());
        // Add references to template argument types as they are written in code, not the canonical types.
        if (const ASTTemplateArgumentListInfo* templateArgs = specInfo->TemplateArgumentsAsWritten) {
            for (unsigned i = 0; i < templateArgs->NumTemplateArgs; ++i) {
                const TemplateArgumentLoc& arg = (*templateArgs)[i];
                insertReferenceToType(f, arg.getTypeSourceInfo());
            }
        }
    }

    insertReferenceToType(f, f->getReturnType());

    insertReference(f, f->getInstantiatedFromMemberFunction());

    if (f->doesThisDeclarationHaveABody() &&
            sourceManager.isInMainFile(f->getLocStart()))
    {
        dbg("Moving to ";
            DeclarationName DeclName = f->getNameInfo().getName();
            std::string FuncName = DeclName.getAsString();
            std::cerr << FuncName << " at " <<
                toString(sourceManager, f->getLocation()) << std::endl;
        );
    }

    return true;
}

bool DependenciesCollector::VisitFunctionTemplateDecl(FunctionTemplateDecl* functionTemplate) {
    insertReference(functionTemplate,
            functionTemplate->getInstantiatedFromMemberTemplate());
    return true;
}

bool DependenciesCollector::VisitCXXMethodDecl(CXXMethodDecl* method) {
    dbg(CAIDE_FUNC);
    insertReference(method, method->getParent());
    if (method->isVirtual()) {
        // Virtual methods may not be called directly. Assume that
        // if we need a class, we need all its virtual methods.
        // TODO: a more detailed analysis (walk the inheritance tree?)
        insertReference(method->getParent(), method);
    }
    return true;
}

bool DependenciesCollector::VisitCXXRecordDecl(CXXRecordDecl* recordDecl) {
    insertReference(recordDecl, recordDecl->getDescribedClassTemplate());
    // No implicit calls to destructors in AST; assume that
    // if a class is used, its destructor is used too.
    insertReference(recordDecl, recordDecl->getDestructor());
    return true;
}

// sizeof, alignof
bool DependenciesCollector::VisitUnaryExprOrTypeTraitExpr(UnaryExprOrTypeTraitExpr* expr) {
    if (expr->isArgumentType())
        insertReferenceToType(getCurrentDecl(), expr->getArgumentType());
    // if the argument is a variable it will be processed as DeclRefExpr
    return true;
}


}
}
