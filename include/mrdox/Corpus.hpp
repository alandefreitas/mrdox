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

#ifndef MRDOX_CORPUS_HPP
#define MRDOX_CORPUS_HPP

#include "jad/Index.hpp"
#include <mrdox/Config.hpp>
#include <mrdox/Errors.hpp>
#include <clang/Tooling/Execution.h>

namespace clang {
namespace mrdox {

/** The collection of declarations in extracted form.
*/
struct Corpus
{
    Corpus() = default;
    Corpus(Corpus&&) noexcept = default;
    Corpus& operator=(Corpus const&) = delete;

    /** Index of all emitted symbols.
    */
    Index Idx;

    /** Table of Info keyed on Symbol ID.
    */
    llvm::StringMap<std::unique_ptr<Info>> USRToInfo;

    /** List of everything
    */
    std::vector<SymbolID> allSymbols;

    //--------------------------------------------
    //
    // Functions
    //
    //--------------------------------------------

    /** Return a pointer to the Info with the specified symbol ID, or nullptr.
    */
    Info const*
    find(
        SymbolID const& id) const noexcept;

    /** Return true if an Info with the specified symbol ID exists.
    */
    bool
    exists(SymbolID const& id) const noexcept
    {
        return find(id) != nullptr;
    }

    /** Return the Info with the specified symbol ID.

        If the id does not exist, the behavior is undefined.
    */
    Info const&
    at(
        SymbolID const& id) const noexcept;

    /** Return the T with the specified symbol ID.

        If the id does not exist, or the type of the
        Info doesn't match T, the behavior is undefined.
    */
    template<class T>
    T const&
    get(
        SymbolID const& id) const noexcept
    {
        auto const& I = at(id);
        assert(I.IT == T::type_id);
        return static_cast<T const&>(I);
    }

    /** Insert Info into the index
    */
    void
    insert(
        Info const& I);

    //--------------------------------------------

    /** Store the Info in the tool results, keyed by SymbolID.
    */
    static
    void
    reportResult(
        tooling::ExecutionContext& exc,
        Info const& I);

    /** Build the intermediate representation of the code being documented.
    */
    static
    std::unique_ptr<Corpus>
    build(
        tooling::ToolExecutor& ex,
        Config const& cfg,
        Reporter& R);
};

} // mrdox
} // clang

#endif
