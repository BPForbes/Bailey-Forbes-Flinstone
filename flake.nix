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
        });
    };
}
