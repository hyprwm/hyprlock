inputs: pkgs: let
  flake = inputs.self.packages.${pkgs.stdenv.hostPlatform.system};
  run_hyprlock_tests = pkgs.writeShellScriptBin "run_hyprlock_tests.sh" ''
    exit_status=1
    for test_config in /etc/hyprlock_configs/*; do
      echo "Running $test_config" >>/tmp/lock_tester_log
      lock_tester --binary $(which hyprlock) --config "$test_config" 2>&1 >>/tmp/lock_tester_log
      exit_status=$?
      if [ $exit_status -ne 0 ]; then
        break
      fi
    done

    echo $exit_status >/tmp/exit_status
    hyprctl dispatch exit
  '';
in {
  tests = pkgs.testers.runNixOSTest {
    name = "hyprlock-tests";

    nodes.machine = {pkgs, ...}: {
      environment.systemPackages = with pkgs; [
        # Programs needed for tests
        flake.lock_tester
        flake.hyprlock
        run_hyprlock_tests
      ];

      # Enabled by default for some reason
      services.speechd.enable = false;

      environment.variables = {
        #"AQ_TRACE" = "1";
        #"HYPRLAND_TRACE" = "1";
        "HYPRLAND_HEADLESS_ONLY" = "1";
        "XDG_RUNTIME_DIR" = "/tmp";
        "XDG_CACHE_HOME" = "/tmp";
      };

      programs.hyprland.enable = true;

      programs.hyprlock = {
        enable = true;
        package = flake.hyprlock;
      };

      # Hyprland config (runs the tester with exec-once)
      environment.etc."hyprland_test.conf".source = "${flake.lock_tester}/share/hypr/hyprland_test.conf";
      environment.etc."hyprlock_configs".source = "${flake.lock_tester}/share/hypr/configs/";
      environment.etc."run_hyprlock_tests.sh".source = "${run_hyprlock_tests}/bin/run_hyprlock_tests.sh";

      networking.dhcpcd.enable = false;

      # Disable portals
      xdg.portal.enable = pkgs.lib.mkForce false;

      # Autologin root into tty
      services.getty.autologinUser = "alice";

      system.stateVersion = "24.11";

      users.users.alice = {
        isNormalUser = true;
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
      # Wait for tty to be up
      machine.wait_for_unit("multi-user.target")

      # Run hyprtester testing framework/suite
      print("Running hyprtester")
      hyprland_exit_status, out = machine.execute("su - alice -c '${pkgs.hyprland}/bin/Hyprland -c /etc/hyprland_test.conf'", timeout=60)
      print(f"Hyprland exited with {hyprland_exit_status}")

      # Copy logs to host
      machine.execute('cp "$(find /tmp/hypr -name *.log | head -1)" /tmp/hyprlog')
      machine.copy_from_vm("/tmp/lock_tester_log")
      machine.copy_from_vm("/tmp/hyprlog")
      machine.copy_from_vm("/tmp/exit_status")

      # Print logs for visibility in CI
      _, out = machine.execute("cat /tmp/hyprlog")
      print(f"Hyprland logs:\n{out}")
      _, out = machine.execute("cat /tmp/lock_tester_log")
      print(f"Lock tester log:\n{out}")
      _, exit_status = machine.execute("cat /tmp/exit_status")
      print(f"Lock tester exited with {exit_status}")

      # Finally - shutdown
      machine.shutdown()
    '';
  };
}
