#include <algorithm>

#include "cmd-profiles.hh"
#include "command.hh"
#include "common-args.hh"
#include "local-fs-store.hh"
#include "logging.hh"
#include "profiles.hh"
#include "store-api.hh"
#include "filetransfer.hh"
#include "eval.hh"
#include "eval-settings.hh"
#include "attr-path.hh"
#include "names.hh"
#include "progress-bar.hh"

using namespace nix;

struct CmdUpgradeNix : MixDryRun, EvalCommand
{
    Path profileDir;
    std::string storePathsUrl = "https://github.com/NixOS/nixpkgs/raw/master/nixos/modules/installer/tools/nix-fallback-paths.nix";

    CmdUpgradeNix()
    {
        addFlag({
            .longName = "profile",
            .shortName = 'p',
            .description = "The path to the Nix profile to upgrade.",
            .labels = {"profile-dir"},
            .handler = {&profileDir}
        });

        addFlag({
            .longName = "nix-store-paths-url",
            .description = "The URL of the file that contains the store paths of the latest Nix release.",
            .labels = {"url"},
            .handler = {&storePathsUrl}
        });
    }

    /**
     * This command is stable before the others
     */
    std::optional<ExperimentalFeature> experimentalFeature() override
    {
        return std::nullopt;
    }

    std::string description() override
    {
        return "upgrade Nix to the stable version declared in Nixpkgs";
    }

    std::string doc() override
    {
        return
          #include "upgrade-nix.md"
          ;
    }

    Category category() override { return catNixInstallation; }

    void run(ref<Store> store) override
    {
        evalSettings.pureEval = true;

        if (profileDir == "") {
            profileDir = getProfileDir(store);
        }

        auto canonProfileDir = canonPath(profileDir, true);

        printInfo("upgrading Nix in profile '%s'", profileDir);

        StorePath storePath = getLatestNix(store);

        auto version = DrvName(storePath.name()).version;

        if (dryRun) {
            stopProgressBar();
            warn("would upgrade to version %s", version);
            return;
        }

        {
            Activity act(*logger, lvlInfo, actUnknown, fmt("downloading '%s'...", store->printStorePath(storePath)));
            store->ensurePath(storePath);
        }

        {
            Activity act(*logger, lvlInfo, actUnknown, fmt("verifying that '%s' works...", store->printStorePath(storePath)));
            auto program = store->printStorePath(storePath) + "/bin/nix-env";
            auto s = runProgram(program, false, {"--version"});
            if (s.find("Nix") == std::string::npos)
                throw Error("could not verify that '%s' works", program);
        }

        stopProgressBar();

        auto const fullStorePath = store->printStorePath(storePath);

        if (pathExists(canonProfileDir + "/manifest.nix")) {

            std::string nixEnvCmd = settings.nixBinDir + "/nix-env";
            Strings upgradeArgs = {
                "--profile",
                this->profileDir,
                "--install",
                fullStorePath,
                "--no-sandbox",
            };

            printTalkative("running %s %s", nixEnvCmd, concatStringsSep(" ", upgradeArgs));
            runProgram(nixEnvCmd, false, upgradeArgs);
        } else if (pathExists(canonProfileDir + "/manifest.json")) {
            this->upgradeNewStyleProfile(store, storePath);
        } else {
            // No I will not use std::unreachable.
            // That is undefined behavior if you're wrong.
            // This will have a better error message and coredump.
            assert(
                false && "tried to upgrade unexpected kind of profile, "
                "we can only handle `user-environment` and `profile`"
            );
        }

        printInfo(ANSI_GREEN "upgrade to version %s done" ANSI_NORMAL, version);
    }

    /* Return the profile in which Nix is installed. */
    Path getProfileDir(ref<Store> store)
    {
        Path where;

        for (auto & dir : tokenizeString<Strings>(getEnv("PATH").value_or(""), ":"))
            if (pathExists(dir + "/nix-env")) {
                where = dir;
                break;
            }

        if (where == "")
            throw Error("couldn't figure out how Nix is installed, so I can't upgrade it");

        printInfo("found Nix in '%s'", where);

        if (where.starts_with("/run/current-system"))
            throw Error("Nix on NixOS must be upgraded via 'nixos-rebuild'");

        Path profileDir = dirOf(where);

        // Resolve profile to /nix/var/nix/profiles/<name> link.
        while (canonPath(profileDir).find("/profiles/") == std::string::npos && isLink(profileDir)) {
            profileDir = readLink(profileDir);
        }

        printInfo("found profile '%s'", profileDir);

        Path userEnv = canonPath(profileDir, true);

        if (baseNameOf(where) != "bin") {
            throw Error("directory '%s' does not appear to be part of a Nix profile (no /bin dir?)", where);
        }

        if (!pathExists(userEnv + "/manifest.nix") && !pathExists(userEnv + "/manifest.json")) {
            throw Error(
                "directory '%s' does not have a compatible profile manifest; was it created by Nix?",
                where
            );
        }

        if (!store->isValidPath(store->parseStorePath(userEnv))) {
            throw Error("directory '%s' is not in the Nix store", userEnv);
        }

        return profileDir;
    }

    // TODO: Is there like, any good naming scheme that distinguishes
    // "profiles which nix-env can use" and "profiles which nix profile can use"?
    // You can't just say the manifest version since v2 and v3 are both the latter.
    void upgradeNewStyleProfile(ref<Store> & store, StorePath const & newNix)
    {
        auto fsStore = store.dynamic_pointer_cast<LocalFSStore>();
        // TODO(Qyriad): this check is here because we need to cast to a LocalFSStore,
        // to pass to createGeneration(), ...but like, there's no way a remote store
        // would work with the nix-env based upgrade either right?
        if (!fsStore) {
            throw Error("nix upgrade-nix cannot be used on a remote store");
        }

        // nb: nothing actually gets evaluated here.
        // The ProfileManifest constructor only evaluates anything for manifest.nix
        // profiles, which this is not.
        auto evalState = this->getEvalState();

        ProfileManifest manifest(*evalState, profileDir);

        // Find which profile element has Nix in it.
        // It should be impossible to *not* have Nix, since we grabbed this
        // store path by looking for things with bin/nix-env in them anyway.
        auto findNix = [&](ProfileElement const & elem) -> bool {
            for (auto const & ePath : elem.storePaths) {
                auto const nixEnv = store->printStorePath(ePath) + "/bin/nix-env";
                if (pathExists(nixEnv)) {
                    return true;
                }
            }
            // We checked each store path in this element. No nixes here boss!
            return false;
        };
        auto elemWithNix = std::find_if(
            manifest.elements.begin(),
            manifest.elements.end(),
            findNix
        );
        // *Should* be impossible...
        assert(elemWithNix != std::end(manifest.elements));

        // Now create a new profile element for the new Nix version...
        ProfileElement elemForNewNix = {
            .storePaths = {newNix},
        };

        // ...and splork it into the manifest where the old profile element was.
        // (Remember, elemWithNix is an iterator)
        *elemWithNix = elemForNewNix;

        // Build the new profile, and switch to it.
        StorePath const newProfile = manifest.build(store);
        printTalkative("built new profile '%s'", store->printStorePath(newProfile));
        auto const newGeneration = createGeneration(*fsStore, this->profileDir, newProfile);
        printTalkative(
            "switching '%s' to newly created generation '%s'",
            this->profileDir,
            newGeneration
        );
        // TODO(Qyriad): use switchGeneration?
        // switchLink's docstring seems to indicate that's preferred, but it's
        // not used for any other `nix profile`-style profile code except for
        // rollback, and it assumes you already have a generation number, which
        // we don't.
        switchLink(profileDir, newGeneration);
    }

    /* Return the store path of the latest stable Nix. */
    StorePath getLatestNix(ref<Store> store)
    {
        Activity act(*logger, lvlInfo, actUnknown, "querying latest Nix version");

        // FIXME: use nixos.org?
        auto req = FileTransferRequest(storePathsUrl);
        auto res = getFileTransfer()->download(req);

        auto state = std::make_unique<EvalState>(SearchPath{}, store);
        auto v = state->allocValue();
        state->eval(state->parseExprFromString(res.data, state->rootPath(CanonPath("/no-such-path"))), *v);
        Bindings & bindings(*state->allocBindings(0));
        auto v2 = findAlongAttrPath(*state, settings.thisSystem, bindings, *v).first;

        return store->parseStorePath(state->forceString(*v2, noPos, "while evaluating the path tho latest nix version"));
    }
};

static auto rCmdUpgradeNix = registerCommand<CmdUpgradeNix>("upgrade-nix");
