self: {
  config,
  pkgs,
  lib,
  ...
}: let
  inherit (builtins) toString;
  inherit (lib.types) bool float int listOf lines nullOr package str submodule;
  inherit (lib.modules) mkIf;
  inherit (lib.options) mkOption mkEnableOption;

  boolToString = x:
    if x
    then "true"
    else "false";
  cfg = config.programs.hyprlock;

  shadow = {
    shadow_passes = mkOption {
      description = "Shadow passes";
      type = int;
      default = 0;
    };
    shadow_size = mkOption {
      description = "Shadow size";
      type = int;
      default = 3;
    };
    shadow_color = mkOption {
      description = "Shadow color";
      type = str;
      default = "rgba(0, 0, 0, 1.0)";
    };
    shadow_boost = mkOption {
      description = "Boost shadow's opacity";
      type = float;
      default = 1.2;
    };
  };
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

    extraConfig = mkOption {
      description = "Extra configuration lines, written verbatim";
      type = nullOr lines;
      default = null;
    };

    sources = mkOption {
      description = "List of files to `source`";
      type = listOf str;
      default = [];
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
      no_fade_out = mkOption {
          description = "Do not fade out";
          type = bool;
          default = false;
      };
      ignore_empty_input = mkOption {
          description = "Skips validation when an empty password is provided";
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

    images = mkOption {
      description = "Image configurations";
      type = listOf (submodule {
        options = {
          monitor = mkOption {
            description = "The monitor to draw an image";
            type = str;
            default = "";
          };

          path = mkOption {
            description = "The path to source image";
            type = str;
            default = "/home/me/cutie.png"; # only png supported for now
          };

          size = mkOption {
            description = "Size of the image. Lesser side is chosen if not 1:1 aspect ratio";
            type = int;
            default = 150;
          };

          rounding = mkOption {
            description = "The rounding of the image";
            type = int;
            default = -1;
          };

          border_size = mkOption {
            description = "Size of image border";
            type = int;
            default = 4;
          };

          border_color = mkOption {
            description = "Color of image border";
            type = str;
            default = "rgb(221, 221, 221)";
          };

          rotate = mkOption {
            description = "Image rotation angle";
            type = float;
            default = 0.0;
          };

          position = {
            x = mkOption {
              description = "X position of the image";
              type = int;
              default = 0;
            };
            y = mkOption {
              description = "Y position of the image";
              type = int;
              default = 200;
            };
          };

          halign = mkOption {
            description = "Horizontal alignment of the image";
            type = str;
            default = "center";
          };

          valign = mkOption {
            description = "Vertical alignment of the image";
            type = str;
            default = "center";
          };
        }
        // shadow;
      });
      default = [];
    };

    input-fields = mkOption {
      description = "Input field configurations";
      type = listOf (submodule {
        options =
          {
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

            dots_rounding = mkOption {
              description = "The rounding of dots (-2 follows input-field rounding)";
              type = int;
              default = -1;
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

            fade_timeout = mkOption {
              description = "Milliseconds before the input field should be faded (0 to fade immediately)";
              type = int;
              default = 1000;
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

            rounding = mkOption {
              description = "The rounding of the input field";
              type = int;
              default = -1;
            };

            check_color = mkOption {
              description = "The outer color of the input field while checking password";
              type = str;
              default = "rgb(204, 136, 34)";
            };

            fail_color = mkOption {
              description = "If authentication failed, changes outer color and fail message color";
              type = str;
              default = "rgb(204, 34, 34)";
            };

            fail_text = mkOption {
              description = "The text shown if authentication failed. $FAIL (reason) and $ATTEMPTS variables are available";
              type = str;
              default = "<i>$FAIL</i>";
            };

            fail_transition = mkOption {
              description = "The transition time (ms) between normal outer color and fail color";
              type = int;
              default = 300;
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

            capslock_color = mkOption {
              description = "Color of the input field when Caps Lock is active";
              type = str;
              default = "-1";
            };

            numlock_color = mkOption {
              description = "Color of the input field when NumLock is active";
              type = str;
              default = "-1";
            };

            bothlock_color = mkOption {
              description = "Color of the input field when both Caps Lock and NumLock are active";
              type = str;
              default = "-1";
            };

            invert_numlock = mkOption {
              description = "Whether to change the color when NumLock is not active";
              type = bool;
              default = false;
            };

            swap_font_color = mkOption {
              description = "Whether to swap font color with inner color on some events";
              type = bool;
              default = false;
            };
          }
          // shadow;
      });
      default = [
        {}
      ];
    };

    labels = mkOption {
      description = "Label configurations";
      type = listOf (submodule {
        options =
          {
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

            rotate = mkOption {
              description = "Label rotation angle";
              type = float;
              default = 0.0;
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
          }
          // shadow;
      });
      default = [];
    };
  };

  config = mkIf cfg.enable {
    home.packages = [cfg.package];

    xdg.configFile."hypr/hyprlock.conf".text = ''
      ${builtins.concatStringsSep "\n" (map (source: ''
        source = ${source}
      '') cfg.sources)}

      general {
        disable_loading_bar = ${boolToString cfg.general.disable_loading_bar}
        grace = ${toString cfg.general.grace}
        hide_cursor = ${boolToString cfg.general.hide_cursor}
        no_fade_in = ${boolToString cfg.general.no_fade_in}
        no_fade_out = ${boolToString cfg.general.no_fade_out}
        ignore_empty_input = ${boolToString cfg.general.ignore_empty_input}
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

      ${builtins.concatStringsSep "\n" (map (image: ''
          image {
            monitor = ${image.monitor}
            path = ${image.path}
            size = ${toString image.size}
            rounding = ${toString image.rounding}
            border_size = ${toString image.border_size}
            border_color = ${image.border_color}
            rotate = ${toString image.rotate}

            position = ${toString image.position.x}, ${toString image.position.y}
            halign = ${image.halign}
            valign = ${image.valign}
          }
        '')
        cfg.images)}

      ${builtins.concatStringsSep "\n" (map (input-field: ''
          input-field {
            monitor = ${input-field.monitor}
            size = ${toString input-field.size.width}, ${toString input-field.size.height}
            outline_thickness = ${toString input-field.outline_thickness}
            dots_size = ${toString input-field.dots_size}
            dots_spacing = ${toString input-field.dots_spacing}
            dots_center = ${boolToString input-field.dots_center}
            dots_rounding = ${toString input-field.dots_rounding}
            outer_color = ${input-field.outer_color}
            inner_color = ${input-field.inner_color}
            font_color = ${input-field.font_color}
            fade_on_empty = ${boolToString input-field.fade_on_empty}
            fade_timeout = ${toString input-field.fade_timeout}
            placeholder_text = ${input-field.placeholder_text}
            hide_input = ${boolToString input-field.hide_input}
            rounding = ${toString input-field.rounding}
            shadow_passes = ${toString input-field.shadow_passes}
            shadow_size = ${toString input-field.shadow_size}
            shadow_color = ${input-field.shadow_color}
            shadow_boost = ${toString input-field.shadow_boost}
            check_color = ${input-field.check_color}
            fail_color = ${input-field.fail_color}
            fail_text = ${input-field.fail_text}
            fail_transition = ${toString input-field.fail_transition}
            capslock_color = ${input-field.capslock_color}
            numlock_color = ${input-field.numlock_color}
            bothlock_color = ${input-field.bothlock_color}
            invert_numlock = ${boolToString input-field.invert_numlock}
            swap_font_color = ${boolToString input-field.swap_font_color}

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
            rotate = ${toString label.rotate}
            shadow_passes = ${toString label.shadow_passes}
            shadow_size = ${toString label.shadow_size}
            shadow_color = ${label.shadow_color}
            shadow_boost = ${toString label.shadow_boost}

            position = ${toString label.position.x}, ${toString label.position.y}
            halign = ${label.halign}
            valign = ${label.valign}
          }
        '')
        cfg.labels)}

        ${lib.optionalString (cfg.extraConfig != null) cfg.extraConfig}
    '';
  };
}
