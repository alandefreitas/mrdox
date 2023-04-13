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

#include "Representation.h"
#include "Namespace.hpp"
#include "base64.hpp"
#include "escape.hpp"
#include <mrdox/Config.hpp>
#include <mrdox/Corpus.hpp>
#include <mrdox/Generator.hpp>
#include <mrdox/Visitor.hpp>
#include <clang/Basic/Specifiers.h>
#include <clang/Index/USRGeneration.h>
#include <clang/Tooling/Execution.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>

namespace clang {
namespace mrdox {

namespace {

//------------------------------------------------

struct XMLGenerator : Generator
{
    llvm::StringRef
    name() const noexcept override
    {
        return "XML";
    }

    llvm::StringRef
    extension() const noexcept override
    {
        return "xml";
    }

    bool
    buildOne(
        llvm::StringRef fileName,
        Corpus& corpus,
        Config const& config,
        Reporter& R) const override;

    bool
    buildString(
        std::string& dest,
        Corpus& corpus,
        Config const& config,
        Reporter& R) const override;
};

//------------------------------------------------

class Writer
{
    Corpus const& corpus_;
    Config const& config_;
    Reporter& R_;
    std::string level_;
    llvm::raw_ostream* os_ = nullptr;

public:
    struct Attr;
    using Attrs = std::initializer_list<Attr>;

    Writer(
        Corpus const& corpus,
        Config const& config,
        Reporter& R) noexcept;

    bool write(llvm::raw_ostream& os);

    void writeAllSymbols();

    void writeNamespaces(std::vector<Reference> const& v);
    void writeRecords(std::vector<Reference> const& v);
    void writeFunctions(std::vector<Reference> const& v);
    void write(std::vector<EnumInfo> const& v);
    void write(std::vector<TypedefInfo> const& v);

    void write(NamespaceInfo const& I);
    void write(RecordInfo const& I);
    void write(FunctionInfo const& I);
    void write(EnumInfo const& I);
    void write(TypedefInfo const& I);
    void write(BaseRecordInfo const& I);
    void writeSymbolInfo(SymbolInfo const& I);
    void writeInfo(Info const& I);

    void write(llvm::ArrayRef<FieldTypeInfo> const& v);
    void write(FieldTypeInfo const& I);
    void writeNamespaceRefs(llvm::SmallVector<Reference, 4> const& v);
    void write(Reference const& ref);

    void write(Location const& loc, bool def = false);

    void openTag(llvm::StringRef);
    void openTag(llvm::StringRef, Attrs);
    void closeTag(llvm::StringRef);
    void writeTag(llvm::StringRef);
    void writeTag(llvm::StringRef, Attrs);
    void writeTagLine(llvm::StringRef tag, llvm::StringRef value);
    void writeTagLine(llvm::StringRef tag, llvm::StringRef value, Attrs);
    void writeAttrs(Attrs attrs);
    void indent();
    void outdent();

    static std::string toString(SymbolID const& id);
    //static llvm::StringRef toString(InfoType) noexcept;

    //--------------------------------------------

    struct Attr
    {
        llvm::StringRef name;
        std::string value;
        bool pred;

        Attr(
            llvm::StringRef name_,
            llvm::StringRef value_,
            bool pred_ = true) noexcept
            : name(name_)
            , value(value_)
            , pred(pred_)
        {
        }

        Attr(SymbolID USR)
            : name("usr")
            , value(toBase64(USR))
            , pred(USR != EmptySID)
        {
        }

        Attr(AccessSpecifier access) noexcept
            : name("access")
            , value(clang::getAccessSpelling(access))
            , pred(access != AccessSpecifier::AS_none)
        {
        }
    };

};

//------------------------------------------------

} // (anon)

} // mrdox
} // clang
