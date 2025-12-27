LOG_FILE="/tmp/hyprlock_ydotool_loop.txt"


EXIT=0
while (( EXIT == 0 )); do
  hyprctl dispatch exec "HW_TRACE=1 HA_TRACE=1 script -q -c \"$1 --config $2\" /dev/null > \"$LOG_FILE\""

  ydotool type "$3\n"
  sleep 3

  grep -q "Authentication failed" "$LOG_FILE"
  if [ $? == 0 ]; then
     echo OK
     pkill -USR1 hyprlock

     sleep 1
  else
     EXIT=1
  fi
done
