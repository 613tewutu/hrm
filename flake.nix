{
  description = "Human Resource Machine C++ development environment";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";

  outputs =
    { nixpkgs, ... }:
    let
      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
    in
    {
      devShells = forAllSystems (
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
        in
        {
          default = pkgs.mkShell {
            # Include raylib's native platform dependencies for xmake source builds.
            inputsFrom = [ pkgs.raylib ];

            nativeBuildInputs = with pkgs; [
              xmake
              cmake
              ninja
              pkg-config
              just
              tmux
              python3
              clang-tools
              ripgrep
              git
              cacert
            ];

            buildInputs = with pkgs; [
              ftxui
              raylib
              yaml-cpp
            ];
          };
        }
      );

      formatter = forAllSystems (system: nixpkgs.legacyPackages.${system}.nixfmt);
    };
}
