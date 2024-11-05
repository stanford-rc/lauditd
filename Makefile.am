ACLOCAL_AMFLAGS = -I .autotools/m4

rpm_dir=`pwd`/rpms
rpmbuild_opt=

SUBDIRS=src

new: clean all

rpms: lauditd.spec dist
	mkdir -p $(rpm_dir)/BUILD $(rpm_dir)/SPECS $(rpm_dir)/SRPMS $(rpm_dir)/RPMS \
	&& rpmbuild $(rpmbuild_opt) --define="_topdir $(rpm_dir)" \
		--define="_prefix $(prefix)" -ta $(distdir).tar.gz \
	&& echo "RPMs successfully generated in $(rpm_dir)"

rpm: lauditd.spec dist
	mkdir -p $(rpm_dir)/BUILD $(rpm_dir)/SPECS $(rpm_dir)/SRPMS $(rpm_dir)/RPMS \
	&& rpmbuild $(rpmbuild_opt) --define="_topdir $(rpm_dir)" \
		--define="_prefix $(prefix)" -tb $(distdir).tar.gz \
	&& echo "RPM successfully generated in $(rpm_dir)/RPMS"

srpm: lauditd.spec dist
	mkdir -p $(rpm_dir)/BUILD $(rpm_dir)/SPECS $(rpm_dir)/SRPMS $(rpm_dir)/RPMS \
	&& rpmbuild $(rpmbuild_opt) --define="_topdir $(rpm_dir)" --define="_prefix $(prefix)" \
		-ts $(distdir).tar.gz \
	&& echo "src RPM successfully generated in $(rpm_dir)/SRPMS"

EXTRA_DIST= lauditd.spec lauditd.spec.in systemd/lauditd systemd/lauditd@.service
