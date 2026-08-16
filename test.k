def cattoshallmeow(x)
  if x < 3 then
    1
  else
    cattoshallmeow(x-1) + cattoshallmeow(x-2);

cattoshallmeow(10);