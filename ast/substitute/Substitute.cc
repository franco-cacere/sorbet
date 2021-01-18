#include "ast/substitute/substitute.h"
#include "ast/Helpers.h"
#include "ast/treemap/treemap.h"
#include "common/typecase.h"
#include "core/GlobalSubstitution.h"
namespace sorbet::ast {

namespace {
class SubstWalk {
private:
    const core::GlobalSubstitution &subst;

    ExpressionPtr substClassName(core::MutableContext ctx, ExpressionPtr node) {
        auto *constLit = cast_tree<UnresolvedConstantLit>(node);
        if (constLit == nullptr) { // uncommon case. something is strange
            if (isa_tree<EmptyTree>(node)) {
                return node;
            }
            return TreeMap::apply(ctx, *this, std::move(node));
        }

        auto scope = substClassName(ctx, std::move(constLit->scope));
        auto cnst = subst.substitute(constLit->cnst);

        return make_expression<UnresolvedConstantLit>(constLit->loc, std::move(scope), cnst);
    }

    ExpressionPtr substArg(core::MutableContext ctx, ExpressionPtr argp) {
        ExpressionPtr *arg = &argp;
        while (arg != nullptr) {
            typecase(
                *arg, [&](RestArg &rest) { arg = &rest.expr; }, [&](KeywordArg &kw) { arg = &kw.expr; },
                [&](OptionalArg &opt) { arg = &opt.expr; }, [&](BlockArg &opt) { arg = &opt.expr; },
                [&](ShadowArg &opt) { arg = &opt.expr; },
                [&](Local &local) {
                    local.localVariable._name = subst.substitute(local.localVariable._name);
                    arg = nullptr;
                },
                [&](const UnresolvedIdent &nm) { Exception::raise("UnresolvedIdent remaining after local_vars"); });
        }
        return argp;
    }

public:
    SubstWalk(const core::GlobalSubstitution &subst) : subst(subst) {}

    ExpressionPtr preTransformClassDef(core::MutableContext ctx, ExpressionPtr tree) {
        auto *original = cast_tree<ClassDef>(tree);
        original->name = substClassName(ctx, std::move(original->name));
        for (auto &anc : original->ancestors) {
            anc = substClassName(ctx, std::move(anc));
        }
        return tree;
    }

    ExpressionPtr preTransformMethodDef(core::MutableContext ctx, ExpressionPtr tree) {
        auto *original = cast_tree<MethodDef>(tree);
        original->name = subst.substitute(original->name);
        for (auto &arg : original->args) {
            arg = substArg(ctx, std::move(arg));
        }
        return tree;
    }

    ExpressionPtr preTransformBlock(core::MutableContext ctx, ExpressionPtr tree) {
        auto *original = cast_tree<Block>(tree);
        for (auto &arg : original->args) {
            arg = substArg(ctx, std::move(arg));
        }
        return tree;
    }

    ExpressionPtr postTransformUnresolvedIdent(core::MutableContext ctx, ExpressionPtr original) {
        cast_tree<UnresolvedIdent>(original)->name = subst.substitute(cast_tree<UnresolvedIdent>(original)->name);
        return original;
    }

    ExpressionPtr postTransformLocal(core::MutableContext ctx, ExpressionPtr local) {
        cast_tree<Local>(local)->localVariable._name = subst.substitute(cast_tree<Local>(local)->localVariable._name);
        return local;
    }

    ExpressionPtr preTransformSend(core::MutableContext ctx, ExpressionPtr original) {
        cast_tree<Send>(original)->fun = subst.substitute(cast_tree<Send>(original)->fun);
        return original;
    }

    ExpressionPtr postTransformLiteral(core::MutableContext ctx, ExpressionPtr tree) {
        auto *original = cast_tree<Literal>(tree);
        if (original->isString(ctx)) {
            auto nameRef = original->asString(ctx);
            // The 'from' and 'to' GlobalState in this substitution will always be the same,
            // because the newly created nameRef reuses our current GlobalState id
            bool allowSameFromTo = true;
            auto newName = subst.substitute(nameRef, allowSameFromTo);
            if (newName == nameRef) {
                return tree;
            }
            return MK::String(original->loc, newName);
        }
        if (original->isSymbol(ctx)) {
            auto nameRef = original->asSymbol(ctx);
            // The 'from' and 'to' GlobalState in this substitution will always be the same,
            // because the newly created nameRef reuses our current GlobalState id
            bool allowSameFromTo = true;
            auto newName = subst.substitute(nameRef, allowSameFromTo);
            if (newName == nameRef) {
                return tree;
            }
            return MK::Symbol(original->loc, newName);
        }
        return tree;
    }

    ExpressionPtr postTransformUnresolvedConstantLit(core::MutableContext ctx, ExpressionPtr tree) {
        auto *original = cast_tree<UnresolvedConstantLit>(tree);
        original->cnst = subst.substitute(original->cnst);
        original->scope = substClassName(ctx, std::move(original->scope));
        return tree;
    }
};
} // namespace

ExpressionPtr Substitute::run(core::MutableContext ctx, const core::GlobalSubstitution &subst, ExpressionPtr what) {
    if (subst.useFastPath()) {
        return what;
    }
    SubstWalk walk(subst);
    what = TreeMap::apply(ctx, walk, std::move(what));
    return what;
}

} // namespace sorbet::ast
