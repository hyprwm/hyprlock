self: {
  config,
  pkgs,
  lib,
  ...
}: let
  inherit (builtins) toString;
  inherit (lib.types) bool float int listOf package str submodule;
  inherit (lib.modules) mkIf;
  inherit (lib.options) mkOption mkEnableOption;

  boolToString = x:
    if x
    then "true"
    else "false";
  cfg = config.programs.hyprlock;
in {
  options.programs.hyprlock = {
    enable =
      mkEnableOption ""
      // {
        description = ''
          Whether to enable Hyprlock, Hyprland's GPU-accelerated lock screen utility.

          Note that PAM must be configured to enable hyprlock to perform
          authentication. The package installed through home-manager
          will *not* be able to unlock the session without this
          configuration.

          On NixOS, it can be enabled using:

          ```nix
          security.pam.services.hyprlock = {};
          ```
        '';
      };

    package = mkOption {
      description = "The hyprlock package";
      type = package;
      default = self.packages.${pkgs.stdenv.hostPlatform.system}.hyprlock;
    };

    general = {
      disable_loading_bar = mkOption {
        description = "Whether to disable loading bar";
        type = bool;
        default = false;
      };
      grace = mkOption {
        description = "Seconds to wait for user input before locking";
        type = int;
        default = 0;
      };
      hide_cursor = mkOption {
        description = "Hides the cursor instead of making it visible";
        type = bool;
        default = true;
      };
      no_fade_in = mkOption {
        description = "Do not fade in";
        type = bool;
        default = false;
      };
    };

    backgrounds = mkOption {
      description = "Background configurations";
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
            default = "echo '/home/me/someImage.png'"; # only png supported for now
          };

          color = mkOption {
            description = "Background color";
            type = str;
            default = "rgba(25, 20, 20, 1.0)";
          };

          blur_size = mkOption {
            description = "Blur size";
            type = int;
            default = 8;
          };

          blur_passes = mkOption {
            description = "Blur passes";
            type = int;
            default = 0;
          };

          noise = mkOption {
            description = "Noise applied to blur";
            type = float;
            default = 0.0117;
          };

          contrast = mkOption {
            description = "Contrast applied to blur";
            type = float;
            default = 0.8917;
          };

          brightness = mkOption {
            description = "Brightness applied to blur";
            type = float;
            default = 0.8172;
          };

          vibrancy = mkOption {
            description = "Vibrancy applied to blur";
            type = float;
            default = 0.1686;
          };

          vibrancy_darkness = mkOption {
            description = "Vibrancy darkness applied to blur";
            type = float;
            default = 0.05;
          };
        };
      });
      default = [
        {}
      ];
    };

    input-fields = mkOption {
      description = "Input field configurations";
      type = listOf (submodule {
        options = {
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

          dots_size = mkOption {
            description = "The size of the dots in the input field (scale of input-field height, 0.2 - 0.8)";
            type = float;
            default = 0.33;
          };

          dots_spacing = mkOption {
            description = "The spacing between the dots in the input field (scale of dot's absolute size, 0.0 - 1.0)";
            type = float;
            default = 0.15;
          };

          dots_center = mkOption {
            description = "Center position of the dots in the input field";
            type = bool;
            default = true;
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
              default = -20;
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
      });
      default = [
        {}
      ];
    };

    labels = mkOption {
      description = "Label configurations";
      type = listOf (submodule {
        options = {
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
      });
      default = [
        {}
      ];
    };
  };

  config = mkIf cfg.enable {
    home.packages = [cfg.package];

    xdg.configFile."hypr/hyprlock.conf".text = ''
      general {
        disable_loading_bar = ${boolToString cfg.general.disable_loading_bar}
        grace = ${toString cfg.general.grace}
        hide_cursor = ${boolToString cfg.general.hide_cursor}
        no_fade_in = ${boolToString cfg.general.no_fade_in}
      }

      ${builtins.concatStringsSep "\n" (map (background: ''
          background {
            monitor = ${background.monitor}
            path = ${background.path}
            color = ${background.color}
            blur_size = ${toString background.blur_size}
            blur_passes = ${toString background.blur_passes}
            noise = ${toString background.noise}
            contrast = ${toString background.contrast}
            brightness = ${toString background.brightness}
            vibrancy = ${toString background.vibrancy}
            vibrancy_darkness = ${toString background.vibrancy_darkness}
          }
        '')
        cfg.backgrounds)}

      ${builtins.concatStringsSep "\n" (map (input-field: ''
          input-field {
            monitor = ${input-field.monitor}
            size = ${toString input-field.size.width}, ${toString input-field.size.height}
            outline_thickness = ${toString input-field.outline_thickness}
            dots_size = ${toString input-field.dots_size}
            dots_spacing = ${toString input-field.dots_spacing}
            dots_center = ${boolToString input-field.dots_center}
            outer_color = ${input-field.outer_color}
            inner_color = ${input-field.inner_color}
            font_color = ${input-field.font_color}
            fade_on_empty = ${boolToString input-field.fade_on_empty}
            placeholder_text = ${input-field.placeholder_text}
            hide_input = ${boolToString input-field.hide_input}

            position = ${toString input-field.position.x}, ${toString input-field.position.y}
            halign = ${input-field.halign}
            valign = ${input-field.valign}
          }
        '')
        cfg.input-fields)}

      ${builtins.concatStringsSep "\n" (map (label: ''
          label {
            monitor = ${label.monitor}
            text = ${label.text}
            color = ${label.color}
            font_size = ${toString label.font_size}
            font_family = ${label.font_family}

            position = ${toString label.position.x}, ${toString label.position.y}
            halign = ${label.halign}
            valign = ${label.valign}
          }
        '')
        cfg.labels)}
    '';
  };
}
