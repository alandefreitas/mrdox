//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Copyright (c) 2023 Vinnie Falco (vinnie.falco@gmail.com)
//
// Official repository: https://github.com/cppalliance/mrdox
//

#include "Options.hpp"
#include "SingleFileDB.hpp"
#include "ConfigImpl.hpp"
#include "CorpusImpl.hpp"
#include "Support/Debug.hpp"
#include "Support/Error.hpp"
#include <mrdox/Config.hpp>
#include <mrdox/Generators.hpp>
#include <mrdox/Support/Report.hpp>
#include <clang/Tooling/StandaloneExecution.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/Program.h>
#include <llvm/Support/Signals.h>
#include <llvm/Support/ThreadPool.h>
#include <atomic>
#include <iomanip>
#include <iostream>
#include <chrono>

namespace clang {
namespace mrdox {

extern void dumpCommentTypes();
extern void dumpCommentCommands();

using SmallString = llvm::SmallString<0>;

//------------------------------------------------

struct Results
{
    using clock_type = std::chrono::system_clock;

    clock_type::time_point startTime;
    std::atomic<std::size_t> numberOfDirs = 0;
    std::atomic<std::size_t> numberOfFiles = 0;
    std::atomic<std::size_t> numberOfErrors = 0;
    std::atomic<std::size_t> numberOfFailures = 0;
    std::atomic<std::size_t> numberofFilesWritten = 0;

    Results()
        : startTime(clock_type::now())
    {
    }

    // Return the number of milliseconds of elapsed time
    auto
    elapsedMilliseconds() const noexcept
    {
        return std::chrono::duration_cast<
            std::chrono::milliseconds>(
                clock_type::now() - startTime).count();
    }
};

//------------------------------------------------

// We need a different config for each directory
// or file passed on the command line, and thus
// each input path must have a separate TestRunner.

class TestRunner
{
    Results& results_;
    std::string extraYaml_;
    std::shared_ptr<Config const> config_;
    Config::WorkGroup wg_;
    llvm::ErrorOr<std::string> diff_;
    Generator const* xmlGen_;
    Generator const* adocGen_;

    std::shared_ptr<Config const>
    makeConfig(
        llvm::StringRef workingDir);

    Error
    writeFile(
        llvm::StringRef filePath,
        llvm::StringRef contents);

    Error
    handleFile(
        llvm::StringRef filePath,
        std::shared_ptr<Config const> const& config);

    Error
    handleDir(
        llvm::StringRef dirPath);

public:
    TestRunner(
        Results& results,
        llvm::StringRef extraYaml);

    /** Check a single file, or a directory recursively.

        This function checks the specified path
        and blocks until completed.
    */
    Error
    checkPath(
        llvm::StringRef inputPath);
};

//------------------------------------------------

TestRunner::
TestRunner(
    Results& results,
    llvm::StringRef extraYaml)
    : results_(results)
    , extraYaml_(extraYaml)
    , config_([&extraYaml]
        {
            std::error_code ec;
            auto config = loadConfigString(
                "", extraYaml.str());
            Assert(config);
            return *config;
        }())
    , wg_(config_.get())
    , diff_(llvm::sys::findProgramByName("diff"))
    , xmlGen_(getGenerators().find("xml"))
    , adocGen_(getGenerators().find("adoc"))
{
    Assert(xmlGen_ != nullptr);
    Assert(adocGen_ != nullptr);
}

std::shared_ptr<Config const>
TestRunner::
makeConfig(
    llvm::StringRef workingDir)
{
    std::string configYaml;
    llvm::raw_string_ostream(configYaml) <<
        "verbose: false\n"
        "source-root: " << workingDir << "\n"
        "with-private: true\n"
        "generator:\n"
        "  xml:\n"
        "    index: false\n"
        "    prolog: true\n";

    std::error_code ec;
    auto config = loadConfigString(
        workingDir, configYaml);
    Assert(config);
    return *config;
}

Error
TestRunner::
writeFile(
    llvm::StringRef filePath,
    llvm::StringRef contents)
{
    std::error_code ec;
    llvm::raw_fd_ostream os(filePath, ec, llvm::sys::fs::OF_None);
    if(ec)
    {
        results_.numberOfErrors++;
        return Error("raw_fd_ostream(\"{}\") returned \"{}\"", filePath, ec);
    }
    os << contents;
    if(os.error())
    {
        results_.numberOfErrors++;
        return Error("raw_fd_ostream::write returned \"{}\"", os.error());
    }
    results_.numberofFilesWritten++;
    return Error::success();
}

Error
TestRunner::
handleFile(
    llvm::StringRef filePath,
    std::shared_ptr<Config const> const& config)
{
    namespace fs = llvm::sys::fs;
    namespace path = llvm::sys::path;

    Assert(path::extension(filePath).compare_insensitive(".cpp") == 0);

    results_.numberOfFiles++;

    SmallString dirPath = filePath;
    path::remove_filename(dirPath);

    SmallString outputPath = filePath;
    path::replace_extension(outputPath, xmlGen_->fileExtension());

    // Build Corpus
    std::unique_ptr<Corpus> corpus;
    {
        SingleFileDB db(dirPath, filePath);
        tooling::StandaloneToolExecutor ex(db, { std::string(filePath) });
        auto result = CorpusImpl::build(ex, config);
        if(! result)
        {
            reportError(result.getError(), "build Corpus for \"{}\"", filePath);
            return Error::success(); // keep going
        }
        corpus = std::move(result.get());
    }

    // Generate XML
    std::string generatedXml;
    if(auto err = xmlGen_->buildOneString(generatedXml, *corpus))
    {
        reportError(err, "build XML string for \"{}\"", filePath);
        results_.numberOfErrors++;
        return Error::success(); // keep going
    }

    if(ToolAction == Action::test)
    {
        // Open and load XML comparison file
        std::unique_ptr<llvm::MemoryBuffer> expectedXml;
        {
            auto result = llvm::MemoryBuffer::getFile(outputPath, false);
            if(! result)
            {
                // File could not be loaded
                results_.numberOfErrors++;

                if( result.getError() != std::errc::no_such_file_or_directory)
                {
                    // Some kind of system problem
                    reportError(
                        Error("MemoryBuffer::getFile(\"{}\") returned \"{}\""),
                        "load the reference XML");
                    return Error::success(); // keep going
                }

                // File does not exist, so write it
                if(auto err = writeFile(outputPath, generatedXml))
                    return Error::success();
            }
            else
            {
                expectedXml = std::move(result.get());
            }
        }

        // Compare the generated output with the expected output
        if( expectedXml &&
            generatedXml != expectedXml->getBuffer())
        {
            // The output did not match
            results_.numberOfFailures++;
            reportError("Test for \"{}\" failed", filePath);

            if(badOption.getValue())
            {
                // Write the .bad.xml file
                auto bad = outputPath;
                path::replace_extension(bad, "bad.xml");
                {
                    std::error_code ec;
                    llvm::raw_fd_ostream os(bad, ec, llvm::sys::fs::OF_None);
                    if (ec)
                    {
                        results_.numberOfErrors++;
                        return Error("raw_fd_ostream(\"{}\") returned \"{}\"", bad, ec);
                    }
                    os << generatedXml;
                }

                // VFALCO We are calling this over and over again instead of once?
                if(! diff_.getError())
                {
                    path::replace_extension(bad, "xml");
                    std::array<llvm::StringRef, 5u> args {
                        diff_.get(), "-u", "--color", bad, outputPath };
                    llvm::sys::ExecuteAndWait(diff_.get(), args);
                }

                // Fix the path for the code that follows
                outputPath.pop_back_n(8);
                path::replace_extension(outputPath, "xml");
            }
        }
        else
        {
            // success
        }
    }
    else if(ToolAction == Action::update)
    {
        // Refresh the expected output file
        if(auto err = writeFile(outputPath, generatedXml))
        {
            results_.numberOfErrors++;
            return Error::success();
        }
    }

    return Error::success();
}

Error
TestRunner::
handleDir(
    llvm::StringRef dirPath)
{
    namespace fs = llvm::sys::fs;
    namespace path = llvm::sys::path;

    results_.numberOfDirs++;

    // Set up directory iterator
    std::error_code ec;
    fs::directory_iterator iter(dirPath, ec, false);
    if(ec)
        return Error("fs::directory_iterator(\"{}\") returned \"{}\"", dirPath, ec);
    fs::directory_iterator const end{};

    auto const config = makeConfig(dirPath);

    while(iter != end)
    {
        if(iter->type() == fs::file_type::directory_file)
        {
            if(auto err = handleDir(iter->path()))
                return err;
        }
        else if(
            iter->type() == fs::file_type::regular_file &&
            path::extension(iter->path()).equals_insensitive(".cpp"))
        {
            wg_.post(
                [this, config, filePath = SmallString(iter->path())]
                {
                    handleFile(filePath, config).operator bool();
                });
        }
        else
        {
            // we don't handle this type
        }
        iter.increment(ec);
        if(ec)
            return Error("directory_iterator::increment returned \"{}\"", ec);
    }
    return Error::success();
}

Error
TestRunner::
checkPath(
    llvm::StringRef inputPath)
{
    namespace fs = llvm::sys::fs;
    namespace path = llvm::sys::path;

    // See if inputPath references a file or directory
    fs::file_status fileStatus;
    if(auto ec = fs::status(inputPath, fileStatus))
    {
        results_.numberOfErrors++;
        return Error("fs::status(\"{}\") returned \"{}\"", inputPath, ec);
    }

    if(fileStatus.type() == fs::file_type::regular_file)
    {
        auto const ext = path::extension(inputPath);
        if(! ext.equals_insensitive(".cpp"))
            return Error("\"{}\" is not a .cpp file");

        // Calculate the workingDir
        SmallString workingDir(inputPath);
        path::remove_filename(workingDir);
        path::remove_dots(workingDir, true);

        auto config = makeConfig(workingDir);
        auto err = handleFile(inputPath, config);
        wg_.wait();
        return err;
    }

    if(fileStatus.type() == fs::file_type::directory_file)
    {
        // Iterate this directory and all its children
        SmallString dirPath(inputPath);
        path::remove_dots(dirPath, true);
        auto err = handleDir(dirPath);
        wg_.wait();
        return err;
    }

    return Error("fs::file_type was not directory_file");
}

int
DoTestAction()
{
    using namespace clang::mrdox;

    std::string extraYaml;
    llvm::raw_string_ostream(extraYaml) <<
        "concurrency: 1\n";
    Results results;
    for(auto const& inputPath : InputPaths)
    {
        TestRunner instance(results, extraYaml);
        if(auto err = instance.checkPath(inputPath))
        {
            reportError(err, "check path \"{}\"", inputPath);
            break;
        }
    }

    auto& os = debug_outs();
    if(results.numberofFilesWritten > 0)
        os <<
            results.numberofFilesWritten << " files written\n";
    os <<
        "Checked " <<
        results.numberOfFiles << " files (" <<
        results.numberOfDirs << " dirs)";
    if( results.numberOfErrors > 0 ||
        results.numberOfFailures > 0)
    {
        if( results.numberOfErrors > 0)
        {
            os <<
                ", with " <<
                results.numberOfErrors << " errors";
            if(results.numberOfFailures > 0)
                os <<
                    " and " << results.numberOfFailures <<
                    " failures";
        }
        else
        {
            os <<
                ", with " <<
                results.numberOfFailures <<
                " failures";
        }
    }
    auto milli = results.elapsedMilliseconds();
    if(milli < 10000)
        os <<
            " in " << milli << " milliseconds\n";
#if 0
    else if(milli < 10000)
        os <<
            " in " << std::setprecision(1) <<
            double(milli) / 1000 << " seconds\n";
#endif
    else
        os <<
            " in " << ((milli + 500) / 1000) <<
            " seconds\n";

    if( results.numberOfFailures > 0 ||
        results.numberOfErrors > 0)
        return EXIT_FAILURE;
    return EXIT_SUCCESS;
}

} // mrdox
} // clang
