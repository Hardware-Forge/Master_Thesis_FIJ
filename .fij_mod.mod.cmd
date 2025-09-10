savedcmd_fij_mod.mod := printf '%s\n'   fij_mod.o | awk '!x[$$0]++ { print("./"$$0) }' > fij_mod.mod
