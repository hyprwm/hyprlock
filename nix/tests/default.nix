inputs: pkgs: let
  inherit (pkgs) lib;
  inherit (lib.lists) flatten;
  flake = inputs.self.packages.${pkgs.stdenv.hostPlatform.system};

  env = {
    #"AQ_TRACE" = "1";
    #"HYPRLAND_TRACE" = "1";
    "HYPRLAND_HEADLESS_ONLY" = "1";
    "XDG_RUNTIME_DIR" = "/tmp";
    "XDG_CACHE_HOME" = "/tmp";
  };

  envAddToSystemdRun = lib.concatStringsSep " " (
    lib.mapAttrsToList (k: v: "--setenv ${k}=${v} ") env
  );

  APITRACE_RECORD = true;
  APITRACE_RECORD_PY = if APITRACE_RECORD then "True" else "False";
in {
  tests = pkgs.testers.runNixOSTest {
    name = "hyprlock-tests";

    nodes.machine = {pkgs, ...}: {
      environment.systemPackages = with pkgs; flatten [
        # Programs needed for tests
        coreutils # date command
        procps # pidof
        (lib.optional APITRACE_RECORD apitrace)
      ];

      # Enabled by default for some reason
      services.speechd.enable = false;

      environment.variables = env;

      programs.hyprland = {
        enable = true;
        #withUWSM = true
      };

      programs.hyprlock = {
        enable = true;
        package = flake.hyprlock;
      };

      networking.dhcpcd.enable = false;

      # Disable portals
      xdg.portal.enable = lib.mkForce false;

      # Autologin root into tty
      services.getty.autologinUser = "alice";

      system.stateVersion = "24.11";

      environment.etc."hyprlock/assets".source = "${flake.hyprlock-test-meta}/share/hypr/assets/";

      users.users.alice = {
        isNormalUser = true;
        # password: abcdefghijklmnopqrstuvwxyz1234567890-=!@#$%^&*()_+[]{};\':\\"]\\|,./<>?`~äöüćńóśź
        hashedPassword = "$y$j9T$s.atBE5..ISB2OoPWrXnU1$.8yaRmR9iBV9e.Q9wM1hG0ciMMYLGhpmDqsJo8Sjiv2";
      };

      virtualisation = {
        cores = 4;
        # Might crash with less
        memorySize = 8192;
        resolution = {
          x = 1920;
          y = 1080;
        };

        # Doesn't seem to do much, thought it would fix XWayland crashing
        qemu.options = ["-vga none -device virtio-gpu-pci"];
      };
    };

    testScript = ''
      from pathlib import Path
      # Wait for tty to be up
      machine.wait_for_unit("multi-user.target")
      # Startup Hyprland as the test compositor for hyprlock
      print("Running Hyprland")
      _, __ = machine.execute("systemd-run -q -u hyprland --uid $(id -u alice) -p RuntimeMaxSec=60 ${envAddToSystemdRun} --setenv PATH=$PATH ${pkgs.hyprland}/bin/Hyprland -c ${flake.hyprlock-test-meta}/share/hypr/hyprland.conf")
      _, __ = machine.execute("sleep 5")
      _, systeminfo = machine.execute("hyprctl --instance 0 systeminfo")
      print(systeminfo)

      for hyprlock_config in Path("${flake.hyprlock-test-meta}/share/hypr/configs/").iterdir():
          print(f"Testing configuration file {hyprlock_config}")
          log_file_path = "/tmp/hyprlock_test_" + hyprlock_config.stem

          hyprlock_cmd = f"hyprlock --config {str(hyprlock_config)} -v 2>&1 >{log_file_path}; echo $? > /tmp/exit_status"
          if ${APITRACE_RECORD_PY}:
              hyprlock_cmd = f"${lib.getExe' pkgs.apitrace "apitrace"} trace --output {log_file_path}.trace --api egl {hyprlock_cmd}"
          _, __ = machine.execute(f"hyprctl --instance 0 dispatch exec '{hyprlock_cmd}'")

          wait_for_lock_exit_status, out = machine.execute("WAYLAND_DISPLAY=wayland-1 ${flake.hyprlock-test-meta}/bin/wait-for-lock")
          print(f"Wait for lock exit code: {wait_for_lock_exit_status}")
          if wait_for_lock_exit_status != 0:
              break

          _, hyprlock_pid = machine.execute("pidof hyprlock")
          print(f"Hyprlock pid {hyprlock_pid}")

          # wrong password
          machine.send_chars("asdf\n")

          _, __ = machine.execute("sleep 3") # default fail_timeout is 2 seconds

          # correct password
          machine.send_chars("abcdefghijklmnopqrstuvwxyz1234567890-=!@#$%^&*()_+[]{};':\"]\\|,./<>?`~")
          machine.send_key("alt_r-a")
          machine.send_key("alt_r-o")
          machine.send_key("alt_r-u")
          machine.send_key("alt_r-apostrophe")
          machine.send_key("c")
          machine.send_key("alt_r-apostrophe")
          machine.send_key("n")
          machine.send_key("alt_r-apostrophe")
          machine.send_key("o")
          machine.send_key("alt_r-apostrophe")
          machine.send_key("s")
          machine.send_key("alt_r-apostrophe")
          machine.send_key("z")
          machine.send_chars("\n")

          _, __ = machine.execute(f"waitpid {hyprlock_pid}")
          _, exit_status = machine.execute("cat /tmp/exit_status")
          print(f"Hyprlock exited with {exit_status}")

          machine.copy_from_vm(log_file_path)
          if ${APITRACE_RECORD_PY}:
              machine.copy_from_vm(log_file_path + ".trace")

          _, out = machine.execute(f"cat {log_file_path}")
          print(f"Hyprlock log:\n{out}")
          _, out = machine.execute(f"cat {log_file_path}")

          if not exit_status or int(exit_status) != 0:
              break


      _, out = machine.execute("hyprctl --instance 0 dispatch exit")
      machine.wait_for_unit("hyprland", timeout=10)

      _, exit_status = machine.execute("cat /tmp/exit_status")
      # For the github runner, just to make sure wen don't accidentally succeed
      if not exit_status.strip():
          _, __ = machine.execute("echo 99 >/tmp/exit_status")
          exit_status = "99"

      machine.copy_from_vm("/tmp/exit_status")
      assert int(exit_status) == 0, f"hyprlock exit code != 0 (exited with {exit_status})"

      # Finally - shutdown
      machine.shutdown()
    '';
  };
}
