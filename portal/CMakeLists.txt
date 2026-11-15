# Device Portal: a small HTTPS service in Python (standard library
# only), off until it is turned on in System > Advanced. Its tests run
# from tests/test_device_portal.sh.
set(portal_dir ${KDE_INSTALL_DATADIR}/raye-settings/device-portal)
install(DIRECTORY rayeportal static DESTINATION ${portal_dir}
        PATTERN __pycache__ EXCLUDE)
install(PROGRAMS device-portal DESTINATION ${RAYE_LIBEXECDIR})
install(FILES units/raye-device-portal.socket units/raye-device-portal.service
              units/raye-device-portal-discovery.service
        DESTINATION ${KDE_INSTALL_SYSTEMDUSERUNITDIR})
install(FILES ufw/raye-device-portal DESTINATION ${KDE_INSTALL_FULL_SYSCONFDIR}/ufw/applications.d)
