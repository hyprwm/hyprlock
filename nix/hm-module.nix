self: {
  config,
  pkgs,
  lib,
  ...
}: let
  inherit (builtins) toString;
  inherit (lib.types) int listOf package str bool submodule;
  inherit (lib.modules) mkIf;
  inherit (lib.options) mkOption mkEnableOption;

  boolToString = x: if x then "true" else "false";
  cfg = config.programs.hyprlock;
in {
  options.programs.hyprlock = {
    enable = mkEnableOption "Hyprlock, Hyprland's GPU-accelerated lock screen utility";

    package = mkOption {
      description = "The hyprlock package";
      type = package;
      default = self.packages.${pkgs.stdenv.hostPlatform.system}.hyprlock;
    };

    backgrounds = mkOption {
      description = "Monitor configurations";
      default = [
        {
          monitor = "";
          path = "";
        }
      ];
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

          color = mkOption {
            description = "Background color";
            type = str;
            default = "rgba(25, 20, 20, 1.0)";
          };
        };
      });
    };

    general = {
      disable_loading_bar = mkOption {
        description = "Whether to disable loading bar";
        type = bool;
        default = false;
      };
      hide_cursor = mkOption {
        description = "Hides the cursor instead of making it visible";
        type = bool;
        default = true;
      };      
    };

    input_field = {
      monitor = mkOption {
        description = "The monitor to place the input field on";
        type = str;
        default = "";
      };

      size = {
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

      font_color = mkOption {
        description = "The font color of the input field";
        type = str;
        default = "rgb(10, 10, 10)";
      };

      fade_on_empty = mkOption {
        description = "Fade input field when empty";
        type = bool;
        default = true;
      };

      placeholder_text = mkOption {
        description = "The placeholder text of the input field";
        type = str;
        default = "<i>Input Password...</i>";
      };

      placeholder_color = mkOption {
        description = "The placeholder text color of the input field";
        type = str;
        default = "rgba(120, 120, 120,0.5)";
      };

      hide_input = mkOption {
        description = "Hide input typed into the input field";
        type = bool;
        default = false;
      };

      position = {
        x = mkOption {
          description = "X position of the label";
          type = int;
          default = 0;
        };
        y = mkOption {
          description = "Y position of the label";
          type = int;
          default = 80;
        };
      };

      halign = mkOption {
        description = "Horizontal alignment of the label";
        type = str;
        default = "center";
      };

      valign = mkOption {
        description = "Vertical alignment of the label";
        type = str;
        default = "center";
      };
    };

    label = {
      monitor = mkOption {
        description = "The monitor to display the label on";
        type = str;
        default = "";
      };

      text = mkOption {
        description = "Text to display in label";
        type = str;
        default = "Hi there, $USER";
      };

      color = mkOption {
        description = "Color of the label";
        type = str;
        default = "rgba(200, 200, 200, 1.0)";
      };

      font_size = mkOption {
        description = "Font size of the label";
        type = int;
        default = 25;
      };

      font_family = mkOption {
        description = "Font family of the label";
        type = str;
        default = "Noto Sans";
      };

      position = {
        x = mkOption {
          description = "X position of the label";
          type = int;
          default = 0;
        };

        y = mkOption {
          description = "Y position of the label";
          type = int;
          default = 80;
        };
      };

      halign = mkOption {
        description = "Horizontal alignment of the label";
        type = str;
        default = "center";
      };

      valign = mkOption {
        description = "Vertical alignment of the label";
        type = str;
        default = "center";
      };
    };
  };

  config = mkIf cfg.enable {
    home.packages = [ cfg.package ];

    xdg.configFile."hypr/hyprlock.conf".text = ''
      general {
        disable_loading_bar = ${boolToString cfg.general.disable_loading_bar}
        hide_cursor = ${boolToString cfg.general.hide_cursor}
      }

      label {
        monitor = ${cfg.label.monitor}
        text = ${cfg.label.text}
        color = ${cfg.label.color}
        font_size = ${toString cfg.label.font_size}
        font_family = ${cfg.label.font_family}

        position = ${toString cfg.label.position.x}, ${toString cfg.label.position.y}
        halign = ${cfg.label.halign}
        valign = ${cfg.label.valign}
      }

      input-field {
        monitor = ${cfg.input_field.monitor}
        size = ${toString cfg.input_field.size.width}, ${toString cfg.input_field.size.height}
        outline_thickness = ${toString cfg.input_field.outline_thickness}
        outer_color = ${cfg.input_field.outer_color}
        inner_color = ${cfg.input_field.inner_color}
        font_color = ${cfg.input_field.font_color}
        fade_on_empty = ${boolToString cfg.input_field.fade_on_empty}
        placeholder-text = ${cfg.input_field.placeholder_text}
        hide_input = ${boolToString cfg.input_field.hide_input}

        position = ${toString cfg.input_field.position.x}, ${toString cfg.input_field.position.y}
        halign = ${cfg.input_field.halign}
        valign = ${cfg.input_field.valign}
      }

      ${builtins.concatStringsSep "\n" (map (background: ''
          background {
            monitor = ${background.monitor}
            path = ${background.path}
            color = ${background.color}
          }
        '')
        cfg.backgrounds)}
    '';
  };
}
