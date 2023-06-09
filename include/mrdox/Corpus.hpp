//
// This is a derivative work. originally part of the LLVM Project.
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (c) 2023 Vinnie Falco (vinnie.falco@gmail.com)
//
// Official repository: https://github.com/cppalliance/mrdox
//

#ifndef MRDOX_API_CORPUS_HPP
#define MRDOX_API_CORPUS_HPP

#include <mrdox/Platform.hpp>
#include <mrdox/Config.hpp>
#include <mrdox/MetadataFwd.hpp>
#include <mrdox/Metadata/Symbols.hpp>
#include <cassert>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace clang {
namespace mrdox {

/** The collection of declarations in extracted form.
*/
class MRDOX_VISIBLE
    Corpus
{
protected:
    explicit
    Corpus(
        Config const& config_) noexcept
        : config(config_)
    {
    }

public:
    /** Destructor.
    */
    MRDOX_DECL
    virtual
    ~Corpus() noexcept;

    //--------------------------------------------

    /** The configuration used to generate this corpus.
    */
    Config const& config;

    /** Return a sorted index of all symbols.
    */
    MRDOX_DECL
    virtual
    std::vector<Info const*> const&
    index() const noexcept = 0;

    /** Return the metadata for the global namespace.
    */
    MRDOX_DECL
    NamespaceInfo const&
    globalNamespace() const noexcept;

    /** Return the Info with the matching ID, or nullptr.
    */
    MRDOX_DECL
    virtual
    Info const*
    find(SymbolID const& id) const noexcept = 0;

    /** Return true if an Info with the specified symbol ID exists.
    */
    bool
    exists(
        SymbolID const& id) const noexcept
    {
        return find(id) != nullptr;
    }

    /** Return the Info with the specified symbol ID.

        If the id does not exist, the behavior is undefined.
    */
    template<class T>
    T const&
    get(
        SymbolID const& id) const noexcept;

    //--------------------------------------------

    /** Base class used to visit elements of the corpus.
    */
    struct Visitor
    {
        MRDOX_DECL virtual ~Visitor() noexcept;

        MRDOX_DECL virtual bool visit(NamespaceInfo const&);
        MRDOX_DECL virtual bool visit(RecordInfo const&);
        MRDOX_DECL virtual bool visit(FunctionInfo const&);
        MRDOX_DECL virtual bool visit(TypedefInfo const&);
        MRDOX_DECL virtual bool visit(EnumInfo const&);
        MRDOX_DECL virtual bool visit(VarInfo const&);
        MRDOX_DECL virtual bool visit(FieldInfo const&);
        MRDOX_DECL virtual bool visit(SpecializationInfo const&);

        MRDOX_DECL virtual bool visit(MemberEnum const&, Access);
        MRDOX_DECL virtual bool visit(MemberFunction const&, Access);
        MRDOX_DECL virtual bool visit(MemberRecord const&, Access);
        MRDOX_DECL virtual bool visit(MemberType const&, Access);
        MRDOX_DECL virtual bool visit(DataMember const&, Access);
        MRDOX_DECL virtual bool visit(StaticDataMember const&, Access);
    };

    /** Traverse the symbol, list, or its children.
    */
    /** @{ */
    MRDOX_DECL bool traverse(Visitor&, Info const& I) const;
    MRDOX_DECL bool traverse(Visitor&, NamespaceInfo const& I) const;
    MRDOX_DECL bool traverse(Visitor&, RecordInfo const& I) const;
    MRDOX_DECL bool traverse(Visitor&, SpecializationInfo const& I) const;
    MRDOX_DECL bool traverse(Visitor&, SymbolID id) const;
    MRDOX_DECL bool traverse(Visitor&, std::vector<SymbolID> const& R) const;
    /** @} */

    //--------------------------------------------

    // KRYSTIAN NOTE: temporary
    MRDOX_DECL
    std::string&
    getFullyQualifiedName(
        const Info& I,
        std::string& temp) const;
};

//------------------------------------------------

template<class T>
T const&
Corpus::
get(
    SymbolID const& id) const noexcept
{
    auto I = find(id);
    assert(I != nullptr);
    if constexpr(std::is_same_v<T, Info>)
    {
        return *I;
    }
    else
    {
        auto const& J = *static_cast<T const*>(I);
        assert(J.Kind == T::kind_id);
        return J;
    }
}

} // mrdox
} // clang

#endif
