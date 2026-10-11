ui_print 'Linux Camera: exact-kernel V4L2 module and broker'
if [ "$(uname -r)" != 5.15.176-gd00ba216ccda ]; then
    abort 'This module was built for 5.15.176-gd00ba216ccda'
fi
set_perm_recursive "$MODPATH" 0 0 0755 0644
set_perm "$MODPATH/service.sh" 0 0 0755
set_perm "$MODPATH/bin/linux-camerad" 0 0 0755
ui_print 'Install the separate LinuxCamera APK, then enable it from its activity.'
