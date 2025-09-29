inputs: pkgs: let
  flake = inputs.self.packages.${pkgs.stdenv.hostPlatform.system};

  env = {
    #"AQ_TRACE" = "1";
    #"HYPRLAND_TRACE" = "1";
    "HYPRLAND_HEADLESS_ONLY" = "1";
    "XDG_RUNTIME_DIR" = "/tmp";
    "XDG_CACHE_HOME" = "/tmp";
  };

  envAddToSystemdRun = pkgs.lib.concatStringsSep " " (
    pkgs.lib.mapAttrsToList (k: v: "--setenv ${k}=${v} ") env
  );
in {
  tests = pkgs.testers.runNixOSTest {
    name = "hyprlock-tests";

    nodes.machine = {pkgs, ...}: {
      environment.systemPackages = with pkgs; [
        # Programs needed for tests
        coreutils # date command
        procps # pidof
        libinput
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
        package = flake.hyprlock-debug;
      };

      networking.dhcpcd.enable = false;

      # Disable portals
      xdg.portal.enable = pkgs.lib.mkForce false;

      # Autologin root into tty
      services.getty.autologinUser = "alice";

      system.stateVersion = "24.11";

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
      # Run hyprtester testing framework/suite
      print("Running Hyprland")
      _, __ = machine.execute("systemd-run -q -u hyprland --uid $(id -u alice) -p RuntimeMaxSec=60 ${envAddToSystemdRun} ${pkgs.hyprland}/bin/Hyprland -c ${flake.lock_tester}/share/hypr/hyprland.conf", timeout=60)

      _, __ = machine.execute("sleep 2")
      _, out = machine.execute("hyprctl --instance 0 systeminfo")
      print(out)
      for hyprlock_config in Path("${flake.lock_tester}/share/hypr/configs/").iterdir():
          print(f"Testing configuration file {hyprlock_config}")
          log_file_path = "/tmp/lock_tester_" + hyprlock_config.stem
          print(log_file_path)
          cmd = f"${pkgs.lib.getExe flake.lock_tester} --binary ${pkgs.lib.getExe flake.hyprlock-debug} --config {str(hyprlock_config)} 2>&1 >>{log_file_path}; echo $? > /tmp/exit_code"
          _, out = machine.execute(f"hyprctl --instance 0 dispatch exec '{cmd}'")
          print(out)
          machine.wait_for_file("/tmp/.session-locked", timeout=30)
          _, tester_pid = machine.execute("pidof lock_tester")
          print(f"Lock tester pid {tester_pid}")
          machine.send_chars("asdf\n") # wrong password
          _, __ = machine.execute("sleep 3")
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
          _, __ = machine.execute(f"waitpid {tester_pid}")
          _, exit_status = machine.execute("cat /tmp/exit_code")
          print(f"Lock tester exited with {exit_status}")
          machine.copy_from_vm(log_file_path)
          _, out = machine.execute(f"cat {log_file_path}")
          print(f"Lock tester log:\n{out}")

      #machine.wait_for_file("/tmp/exit_status", timeout=30)
      machine.wait_for_unit("hyprland", timeout=30)

      # Copy logs to host
      #machine.execute('cp "$(find /tmp/hypr -name *.log | head -1)" /tmp/hyprlog')
      #machine.copy_from_vm("/tmp/hyprlog")
      #machine.copy_from_vm("/tmp/exit_status")

      # Print logs for visibility in CI
      #_, out = machine.execute("cat /tmp/hyprlog")
      #print(f"Hyprland logs:\n{out}")

      # Finally - shutdown
      machine.shutdown()
    '';
  };
}
