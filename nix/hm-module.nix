self: {
  config,
  pkgs,
  lib,
  ...
}: let
  inherit (builtins) toString;
  inherit (lib.types) bool float int listOf lines nullOr str submodule;
  inherit (lib.modules) mkIf;
  inherit (lib.options) mkOption mkEnableOption;
  inherit (lib.trivial) boolToString;
  concatMapLines = lib.concatMapStringsSep "\n";

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

    package = lib.mkPackageOption self.packages.${pkgs.stdenv.hostPlatform.system} "hyprlock" {};

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

    shapes = mkOption {
      description = "Shape configurations";
      type = listOf (submodule {
        options =
          {
            monitor = mkOption {
              description = "The monitor to draw a shape";
              type = str;
              default = "";
            };

            size = {
              x = mkOption {
                description = "Width of the shape";
                type = int;
                default = 360;
              };
              y = mkOption {
                description = "Height of the shape";
                type = int;
                default = 60;
              };
            };

            color = mkOption {
              description = "Color of the shape";
              type = str;
              default = "rgba(22, 17, 17, 1.0)";
            };

            rounding = mkOption {
              description = "Rounding of the shape";
              type = int;
              default = -1;
            };

            border_size = mkOption {
              description = "Size of shape border";
              type = int;
              default = 4;
            };

            border_color = mkOption {
              description = "Color of shape border";
              type = str;
              default = "rgba(0, 207, 230, 1.0)";
            };

            rotate = mkOption {
              description = "Shape rotation angle";
              type = float;
              default = 0.0;
            };

            xray = mkOption {
              description = "Whether to make a transparent \"hole\" in the background";
              type = bool;
              default = false;
            };

            position = {
              x = mkOption {
                description = "X position of the shape";
                type = int;
                default = 0;
              };
              y = mkOption {
                description = "Y position of the shape";
                type = int;
                default = 80;
              };
            };

            halign = mkOption {
              description = "Horizontal alignment of the shape";
              type = str;
              default = "center";
            };

            valign = mkOption {
              description = "Vertical alignment of the shape";
              type = str;
              default = "center";
            };
          }
          // shadow;
      });
      default = [];
    };

    images = mkOption {
      description = "Image configurations";
      type = listOf (submodule {
        options =
          {
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

            reload_time = mkOption {
              description = "Interval in seconds between reloading the image";
              type = int;
              default = -1;
            };

            reload_cmd = mkOption {
              description = "Command to obtain new path";
              type = str;
              default = "";
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
              default = 2000;
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

            text_align = mkOption {
              description = "Horizontal alignment of multi-line text";
              type = str;
              default = "";
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

    xdg.configFile."hypr/hyprlock.conf".text = let
      translateCfg = attrs:
        lib.mapAttrsToList (
          n: v: "  ${n} = ${
            if builtins.isNull v
            then ""
            else if builtins.isString v
            then v
            else if builtins.isBool v
            then boolToString v
            else if (builtins.isInt v || builtins.isFloat v)
            then toString v
            else if builtins.isAttrs v
            then "${toString v.x or v.width}, ${toString v.y or v.height}"
            else
              throw ''
                Unhandled attribute value passed to hyprlock of type: ${builtins.typeOf n}
              ''
          }"
        )
        attrs;
    in ''
      ${concatMapLines (source: "source = ${source}") cfg.sources}

      general {
      ${lib.concatLines (translateCfg cfg.general)}
      }

      ${
        concatMapLines (x: (lib.concatMapStrings (
            y: ''
              ${x} {
              ${lib.concatLines (translateCfg y)}
              }
            ''
          )
          cfg."${x}s"))
        ["background" "shape" "image" "input-field" "label"]
      }
      ${lib.optionalString (cfg.extraConfig != null) cfg.extraConfig}
    '';
  };
}
