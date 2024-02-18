self: {
  config,
  pkgs,
  lib,
  ...
}: let
  inherit (builtins) toString;
  inherit (lib.types) int listOf package str submodule;
  inherit (lib.modules) mkIf;
  inherit (lib.options) mkOption mkEnableOption;

  cfg = config.services.hyprlock;
in {
  options.services.hyprlock = {
    enable = mkEnableOption "Hyprlock, Hyprland's GPU-accelerated lock screen utility";

    package = mkOption {
      description = "The hyprlock package";
      type = package;
      default = self.packages.${pkgs.stdenv.hostPlatform.system}.hyprlock;
    };

    backgrounds = mkOption {
      description = "Monitor configurations";
      type = listOf (submodule {
        options = {
          monitor = mkOption {
            description = "The monitor to apply the given wallpaper to";
            type = str;
            default = "";
          };

          path = mkOption {
            description = "The path to the wallpaper";
            type = str;
            default = "echo 'timeout reached'";
          };
        };
      });
    };

    general.disable_loading_bar =
      mkEnableOption ""
      // {
        description = "Whether to disable loading bar";
      };

    input_field = submodule {
      options = {
        monitor = mkOption {
          description = "The monitor to place the input field on";
          type = str;
          default = "";
        };

        size = mkOption {
          description = "The size of the input field";
          type = submodule {
            options = {
              width = mkOption {
                description = "Width of the input field";
                type = int;
                default = 200;
              };
              height = mkOption {
                description = "Height of the input field";
                type = int;
                default = 50;
              };
            };
          };
        };

        outline_thickness = mkOption {
          description = "The outline thickness of the input field";
          type = int;
          default = 3;
        };

        outer_color = mkOption {
          description = "The outer color of the input field";
          type = str;
          default = "rgb(151515)";
        };

        inner_color = mkOption {
          description = "The inner color of the input field";
          type = str;
          default = "rgb(200, 200, 200)";
        };
      };
    };
  };

  config = mkIf cfg.enable {
    xdg.configFile."hypr/hyprlock.conf".text = ''
      general {
        disable_loading_bar = ${cfg.general.disable_loading_bar}
      }

      input_field {
        monitor = ${cfg.input_field.monitor}
        size = ${toString cfg.input_field.size.width} ${toString cfg.input_field.size.height}
        outline_thickness = ${toString cfg.input_field.outline_thickness}
        outer_color = ${cfg.input_field.outer_color}
        inner_color = ${cfg.input_field.inner_color}
      }

      ${builtins.concatStringsSep "\n" (map (background: ''
          background {
            monitor = ${background.monitor}
            path = ${background.path}
          }
        '')
        cfg.backgrounds)}
    '';
  };
}
