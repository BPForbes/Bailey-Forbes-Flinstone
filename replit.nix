# Replit system dependencies for Bailey-Forbes-Flinstone.
# See nix/deps.json (manifest) and docs/replit.md.
# Import on Replit: https://replit.com/~ — requires .replit + replit.nix (GitHub import).
{ pkgs }: {
  deps = [
    pkgs.gcc
    pkgs.gnumake
    pkgs.binutils
    pkgs.nasm
    pkgs.pkg-config
    pkgs.sqlite
    pkgs.openssl
    pkgs.libcunit
    pkgs.SDL2
    pkgs.curl
    pkgs.cacert
    pkgs.cmake
    pkgs.autoconf
    pkgs.automake
    pkgs.libtool
    pkgs.bzip2
    pkgs.gnutar
    pkgs.python3
    pkgs.bashInteractive
  ];
}
