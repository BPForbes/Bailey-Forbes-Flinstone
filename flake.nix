{
  description = "Bailey-Forbes-Flinstone — dev shells (local Nix; Replit uses replit.nix)";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

  outputs = { self, nixpkgs }:
    let
      forAllSystems = nixpkgs.lib.genAttrs [ "x86_64-linux" "aarch64-linux" ];

      resolvePackages = pkgs: names:
        map (name:
          pkgs.${name} or builtins.throw "unknown package '${name}' on ${pkgs.system}"
        ) names;

      baseNative = pkgs: resolvePackages pkgs [
        "pkg-config" "curl" "cacert" "cmake" "autoconf" "automake" "libtool"
        "bzip2" "gnutar" "nasm" "gnumake" "gcc" "binutils"
      ];

      baseBuild = pkgs: resolvePackages pkgs [ "sqlite" "openssl" ];

      crossAarch64Shell = pkgs:
        let
          cross = pkgs.pkgsCross.aarch64-multiplatform;
        in pkgs.mkShell {
          buildInputs = baseBuild cross;
          nativeBuildInputs = baseNative pkgs ++ [ cross.stdenv.cc ];
        };

    in {
      devShells = forAllSystems (system:
        let pkgs = import nixpkgs { inherit system; };
        in {
          default = pkgs.mkShell {
            buildInputs = baseBuild pkgs;
            nativeBuildInputs = baseNative pkgs;
          };
          vm-sdl = pkgs.mkShell {
            buildInputs = baseBuild pkgs ++ [ pkgs.SDL2 ];
            nativeBuildInputs = baseNative pkgs;
          };
          tests = pkgs.mkShell {
            buildInputs = baseBuild pkgs ++ [ pkgs.libcunit ];
            nativeBuildInputs = baseNative pkgs;
          };
          cross-aarch64 =
            if system == "x86_64-linux" then crossAarch64Shell pkgs
            else pkgs.mkShell {
              buildInputs = baseBuild pkgs;
              nativeBuildInputs = baseNative pkgs;
            };
        });
    };
}
