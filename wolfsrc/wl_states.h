// WL_STATES.H -- made by msdos/mkstates.py: don't edit
//
// Every statetype, in order, for wl_net.c's deathmatch snapshots

STATE(s_player)
STATE(s_attack)
STATE(s_rocket)
STATE(s_smoke1)
STATE(s_smoke2)
STATE(s_smoke3)
STATE(s_smoke4)
STATE(s_boom1)
STATE(s_boom2)
STATE(s_boom3)
#if (defined(SPEAR))
STATE(s_hrocket)
#endif
#if (defined(SPEAR))
STATE(s_hsmoke1)
#endif
#if (defined(SPEAR))
STATE(s_hsmoke2)
#endif
#if (defined(SPEAR))
STATE(s_hsmoke3)
#endif
#if (defined(SPEAR))
STATE(s_hsmoke4)
#endif
#if (defined(SPEAR))
STATE(s_hboom1)
#endif
#if (defined(SPEAR))
STATE(s_hboom2)
#endif
#if (defined(SPEAR))
STATE(s_hboom3)
#endif
STATE(s_grdstand)
STATE(s_grdpath1)
STATE(s_grdpath1s)
STATE(s_grdpath2)
STATE(s_grdpath3)
STATE(s_grdpath3s)
STATE(s_grdpath4)
STATE(s_grdpain)
STATE(s_grdpain1)
STATE(s_grdshoot1)
STATE(s_grdshoot2)
STATE(s_grdshoot3)
STATE(s_grdchase1)
STATE(s_grdchase1s)
STATE(s_grdchase2)
STATE(s_grdchase3)
STATE(s_grdchase3s)
STATE(s_grdchase4)
STATE(s_grddie1)
STATE(s_grddie2)
STATE(s_grddie3)
STATE(s_grddie4)
#if (!defined(SPEAR))
STATE(s_blinkychase1)
#endif
#if (!defined(SPEAR))
STATE(s_blinkychase2)
#endif
#if (!defined(SPEAR))
STATE(s_inkychase1)
#endif
#if (!defined(SPEAR))
STATE(s_inkychase2)
#endif
#if (!defined(SPEAR))
STATE(s_pinkychase1)
#endif
#if (!defined(SPEAR))
STATE(s_pinkychase2)
#endif
#if (!defined(SPEAR))
STATE(s_clydechase1)
#endif
#if (!defined(SPEAR))
STATE(s_clydechase2)
#endif
STATE(s_dogpath1)
STATE(s_dogpath1s)
STATE(s_dogpath2)
STATE(s_dogpath3)
STATE(s_dogpath3s)
STATE(s_dogpath4)
STATE(s_dogjump1)
STATE(s_dogjump2)
STATE(s_dogjump3)
STATE(s_dogjump4)
STATE(s_dogjump5)
STATE(s_dogchase1)
STATE(s_dogchase1s)
STATE(s_dogchase2)
STATE(s_dogchase3)
STATE(s_dogchase3s)
STATE(s_dogchase4)
STATE(s_dogdie1)
STATE(s_dogdie2)
STATE(s_dogdie3)
STATE(s_dogdead)
STATE(s_ofcstand)
STATE(s_ofcpath1)
STATE(s_ofcpath1s)
STATE(s_ofcpath2)
STATE(s_ofcpath3)
STATE(s_ofcpath3s)
STATE(s_ofcpath4)
STATE(s_ofcpain)
STATE(s_ofcpain1)
STATE(s_ofcshoot1)
STATE(s_ofcshoot2)
STATE(s_ofcshoot3)
STATE(s_ofcchase1)
STATE(s_ofcchase1s)
STATE(s_ofcchase2)
STATE(s_ofcchase3)
STATE(s_ofcchase3s)
STATE(s_ofcchase4)
STATE(s_ofcdie1)
STATE(s_ofcdie2)
STATE(s_ofcdie3)
STATE(s_ofcdie4)
STATE(s_ofcdie5)
STATE(s_mutstand)
STATE(s_mutpath1)
STATE(s_mutpath1s)
STATE(s_mutpath2)
STATE(s_mutpath3)
STATE(s_mutpath3s)
STATE(s_mutpath4)
STATE(s_mutpain)
STATE(s_mutpain1)
STATE(s_mutshoot1)
STATE(s_mutshoot2)
STATE(s_mutshoot3)
STATE(s_mutshoot4)
STATE(s_mutchase1)
STATE(s_mutchase1s)
STATE(s_mutchase2)
STATE(s_mutchase3)
STATE(s_mutchase3s)
STATE(s_mutchase4)
STATE(s_mutdie1)
STATE(s_mutdie2)
STATE(s_mutdie3)
STATE(s_mutdie4)
STATE(s_mutdie5)
STATE(s_ssstand)
STATE(s_sspath1)
STATE(s_sspath1s)
STATE(s_sspath2)
STATE(s_sspath3)
STATE(s_sspath3s)
STATE(s_sspath4)
STATE(s_sspain)
STATE(s_sspain1)
STATE(s_ssshoot1)
STATE(s_ssshoot2)
STATE(s_ssshoot3)
STATE(s_ssshoot4)
STATE(s_ssshoot5)
STATE(s_ssshoot6)
STATE(s_ssshoot7)
STATE(s_ssshoot8)
STATE(s_ssshoot9)
STATE(s_sschase1)
STATE(s_sschase1s)
STATE(s_sschase2)
STATE(s_sschase3)
STATE(s_sschase3s)
STATE(s_sschase4)
STATE(s_ssdie1)
STATE(s_ssdie2)
STATE(s_ssdie3)
STATE(s_ssdie4)
#if (!defined(SPEAR))
STATE(s_bossstand)
#endif
#if (!defined(SPEAR))
STATE(s_bosschase1)
#endif
#if (!defined(SPEAR))
STATE(s_bosschase1s)
#endif
#if (!defined(SPEAR))
STATE(s_bosschase2)
#endif
#if (!defined(SPEAR))
STATE(s_bosschase3)
#endif
#if (!defined(SPEAR))
STATE(s_bosschase3s)
#endif
#if (!defined(SPEAR))
STATE(s_bosschase4)
#endif
#if (!defined(SPEAR))
STATE(s_bossdie1)
#endif
#if (!defined(SPEAR))
STATE(s_bossdie2)
#endif
#if (!defined(SPEAR))
STATE(s_bossdie3)
#endif
#if (!defined(SPEAR))
STATE(s_bossdie4)
#endif
#if (!defined(SPEAR))
STATE(s_bossshoot1)
#endif
#if (!defined(SPEAR))
STATE(s_bossshoot2)
#endif
#if (!defined(SPEAR))
STATE(s_bossshoot3)
#endif
#if (!defined(SPEAR))
STATE(s_bossshoot4)
#endif
#if (!defined(SPEAR))
STATE(s_bossshoot5)
#endif
#if (!defined(SPEAR))
STATE(s_bossshoot6)
#endif
#if (!defined(SPEAR))
STATE(s_bossshoot7)
#endif
#if (!defined(SPEAR))
STATE(s_bossshoot8)
#endif
#if (!defined(SPEAR))
STATE(s_gretelstand)
#endif
#if (!defined(SPEAR))
STATE(s_gretelchase1)
#endif
#if (!defined(SPEAR))
STATE(s_gretelchase1s)
#endif
#if (!defined(SPEAR))
STATE(s_gretelchase2)
#endif
#if (!defined(SPEAR))
STATE(s_gretelchase3)
#endif
#if (!defined(SPEAR))
STATE(s_gretelchase3s)
#endif
#if (!defined(SPEAR))
STATE(s_gretelchase4)
#endif
#if (!defined(SPEAR))
STATE(s_greteldie1)
#endif
#if (!defined(SPEAR))
STATE(s_greteldie2)
#endif
#if (!defined(SPEAR))
STATE(s_greteldie3)
#endif
#if (!defined(SPEAR))
STATE(s_greteldie4)
#endif
#if (!defined(SPEAR))
STATE(s_gretelshoot1)
#endif
#if (!defined(SPEAR))
STATE(s_gretelshoot2)
#endif
#if (!defined(SPEAR))
STATE(s_gretelshoot3)
#endif
#if (!defined(SPEAR))
STATE(s_gretelshoot4)
#endif
#if (!defined(SPEAR))
STATE(s_gretelshoot5)
#endif
#if (!defined(SPEAR))
STATE(s_gretelshoot6)
#endif
#if (!defined(SPEAR))
STATE(s_gretelshoot7)
#endif
#if (!defined(SPEAR))
STATE(s_gretelshoot8)
#endif
#if (defined(SPEAR))
STATE(s_transstand)
#endif
#if (defined(SPEAR))
STATE(s_transchase1)
#endif
#if (defined(SPEAR))
STATE(s_transchase1s)
#endif
#if (defined(SPEAR))
STATE(s_transchase2)
#endif
#if (defined(SPEAR))
STATE(s_transchase3)
#endif
#if (defined(SPEAR))
STATE(s_transchase3s)
#endif
#if (defined(SPEAR))
STATE(s_transchase4)
#endif
#if (defined(SPEAR))
STATE(s_transdie0)
#endif
#if (defined(SPEAR))
STATE(s_transdie01)
#endif
#if (defined(SPEAR))
STATE(s_transdie1)
#endif
#if (defined(SPEAR))
STATE(s_transdie2)
#endif
#if (defined(SPEAR))
STATE(s_transdie3)
#endif
#if (defined(SPEAR))
STATE(s_transdie4)
#endif
#if (defined(SPEAR))
STATE(s_transshoot1)
#endif
#if (defined(SPEAR))
STATE(s_transshoot2)
#endif
#if (defined(SPEAR))
STATE(s_transshoot3)
#endif
#if (defined(SPEAR))
STATE(s_transshoot4)
#endif
#if (defined(SPEAR))
STATE(s_transshoot5)
#endif
#if (defined(SPEAR))
STATE(s_transshoot6)
#endif
#if (defined(SPEAR))
STATE(s_transshoot7)
#endif
#if (defined(SPEAR))
STATE(s_transshoot8)
#endif
#if (defined(SPEAR))
STATE(s_uberstand)
#endif
#if (defined(SPEAR))
STATE(s_uberchase1)
#endif
#if (defined(SPEAR))
STATE(s_uberchase1s)
#endif
#if (defined(SPEAR))
STATE(s_uberchase2)
#endif
#if (defined(SPEAR))
STATE(s_uberchase3)
#endif
#if (defined(SPEAR))
STATE(s_uberchase3s)
#endif
#if (defined(SPEAR))
STATE(s_uberchase4)
#endif
#if (defined(SPEAR))
STATE(s_uberdie0)
#endif
#if (defined(SPEAR))
STATE(s_uberdie01)
#endif
#if (defined(SPEAR))
STATE(s_uberdie1)
#endif
#if (defined(SPEAR))
STATE(s_uberdie2)
#endif
#if (defined(SPEAR))
STATE(s_uberdie3)
#endif
#if (defined(SPEAR))
STATE(s_uberdie4)
#endif
#if (defined(SPEAR))
STATE(s_uberdie5)
#endif
#if (defined(SPEAR))
STATE(s_ubershoot1)
#endif
#if (defined(SPEAR))
STATE(s_ubershoot2)
#endif
#if (defined(SPEAR))
STATE(s_ubershoot3)
#endif
#if (defined(SPEAR))
STATE(s_ubershoot4)
#endif
#if (defined(SPEAR))
STATE(s_ubershoot5)
#endif
#if (defined(SPEAR))
STATE(s_ubershoot6)
#endif
#if (defined(SPEAR))
STATE(s_ubershoot7)
#endif
#if (defined(SPEAR))
STATE(s_willstand)
#endif
#if (defined(SPEAR))
STATE(s_willchase1)
#endif
#if (defined(SPEAR))
STATE(s_willchase1s)
#endif
#if (defined(SPEAR))
STATE(s_willchase2)
#endif
#if (defined(SPEAR))
STATE(s_willchase3)
#endif
#if (defined(SPEAR))
STATE(s_willchase3s)
#endif
#if (defined(SPEAR))
STATE(s_willchase4)
#endif
#if (defined(SPEAR))
STATE(s_willdeathcam)
#endif
#if (defined(SPEAR))
STATE(s_willdie1)
#endif
#if (defined(SPEAR))
STATE(s_willdie2)
#endif
#if (defined(SPEAR))
STATE(s_willdie3)
#endif
#if (defined(SPEAR))
STATE(s_willdie4)
#endif
#if (defined(SPEAR))
STATE(s_willdie5)
#endif
#if (defined(SPEAR))
STATE(s_willdie6)
#endif
#if (defined(SPEAR))
STATE(s_willshoot1)
#endif
#if (defined(SPEAR))
STATE(s_willshoot2)
#endif
#if (defined(SPEAR))
STATE(s_willshoot3)
#endif
#if (defined(SPEAR))
STATE(s_willshoot4)
#endif
#if (defined(SPEAR))
STATE(s_willshoot5)
#endif
#if (defined(SPEAR))
STATE(s_willshoot6)
#endif
#if (defined(SPEAR))
STATE(s_deathstand)
#endif
#if (defined(SPEAR))
STATE(s_deathchase1)
#endif
#if (defined(SPEAR))
STATE(s_deathchase1s)
#endif
#if (defined(SPEAR))
STATE(s_deathchase2)
#endif
#if (defined(SPEAR))
STATE(s_deathchase3)
#endif
#if (defined(SPEAR))
STATE(s_deathchase3s)
#endif
#if (defined(SPEAR))
STATE(s_deathchase4)
#endif
#if (defined(SPEAR))
STATE(s_deathdeathcam)
#endif
#if (defined(SPEAR))
STATE(s_deathdie1)
#endif
#if (defined(SPEAR))
STATE(s_deathdie2)
#endif
#if (defined(SPEAR))
STATE(s_deathdie3)
#endif
#if (defined(SPEAR))
STATE(s_deathdie4)
#endif
#if (defined(SPEAR))
STATE(s_deathdie5)
#endif
#if (defined(SPEAR))
STATE(s_deathdie6)
#endif
#if (defined(SPEAR))
STATE(s_deathdie7)
#endif
#if (defined(SPEAR))
STATE(s_deathdie8)
#endif
#if (defined(SPEAR))
STATE(s_deathdie9)
#endif
#if (defined(SPEAR))
STATE(s_deathshoot1)
#endif
#if (defined(SPEAR))
STATE(s_deathshoot2)
#endif
#if (defined(SPEAR))
STATE(s_deathshoot3)
#endif
#if (defined(SPEAR))
STATE(s_deathshoot4)
#endif
#if (defined(SPEAR))
STATE(s_deathshoot5)
#endif
#if (defined(SPEAR))
STATE(s_angelstand)
#endif
#if (defined(SPEAR))
STATE(s_angelchase1)
#endif
#if (defined(SPEAR))
STATE(s_angelchase1s)
#endif
#if (defined(SPEAR))
STATE(s_angelchase2)
#endif
#if (defined(SPEAR))
STATE(s_angelchase3)
#endif
#if (defined(SPEAR))
STATE(s_angelchase3s)
#endif
#if (defined(SPEAR))
STATE(s_angelchase4)
#endif
#if (defined(SPEAR))
STATE(s_angeldie1)
#endif
#if (defined(SPEAR))
STATE(s_angeldie11)
#endif
#if (defined(SPEAR))
STATE(s_angeldie2)
#endif
#if (defined(SPEAR))
STATE(s_angeldie3)
#endif
#if (defined(SPEAR))
STATE(s_angeldie4)
#endif
#if (defined(SPEAR))
STATE(s_angeldie5)
#endif
#if (defined(SPEAR))
STATE(s_angeldie6)
#endif
#if (defined(SPEAR))
STATE(s_angeldie7)
#endif
#if (defined(SPEAR))
STATE(s_angeldie8)
#endif
#if (defined(SPEAR))
STATE(s_angeldie9)
#endif
#if (defined(SPEAR))
STATE(s_angelshoot1)
#endif
#if (defined(SPEAR))
STATE(s_angelshoot2)
#endif
#if (defined(SPEAR))
STATE(s_angelshoot3)
#endif
#if (defined(SPEAR))
STATE(s_angeltired)
#endif
#if (defined(SPEAR))
STATE(s_angeltired2)
#endif
#if (defined(SPEAR))
STATE(s_angeltired3)
#endif
#if (defined(SPEAR))
STATE(s_angeltired4)
#endif
#if (defined(SPEAR))
STATE(s_angeltired5)
#endif
#if (defined(SPEAR))
STATE(s_angeltired6)
#endif
#if (defined(SPEAR))
STATE(s_angeltired7)
#endif
#if (defined(SPEAR))
STATE(s_spark1)
#endif
#if (defined(SPEAR))
STATE(s_spark2)
#endif
#if (defined(SPEAR))
STATE(s_spark3)
#endif
#if (defined(SPEAR))
STATE(s_spark4)
#endif
#if (defined(SPEAR))
STATE(s_spectrewait1)
#endif
#if (defined(SPEAR))
STATE(s_spectrewait2)
#endif
#if (defined(SPEAR))
STATE(s_spectrewait3)
#endif
#if (defined(SPEAR))
STATE(s_spectrewait4)
#endif
#if (defined(SPEAR))
STATE(s_spectrechase1)
#endif
#if (defined(SPEAR))
STATE(s_spectrechase2)
#endif
#if (defined(SPEAR))
STATE(s_spectrechase3)
#endif
#if (defined(SPEAR))
STATE(s_spectrechase4)
#endif
#if (defined(SPEAR))
STATE(s_spectredie1)
#endif
#if (defined(SPEAR))
STATE(s_spectredie2)
#endif
#if (defined(SPEAR))
STATE(s_spectredie3)
#endif
#if (defined(SPEAR))
STATE(s_spectredie4)
#endif
#if (defined(SPEAR))
STATE(s_spectrewake)
#endif
#if (!defined(SPEAR))
STATE(s_schabbstand)
#endif
#if (!defined(SPEAR))
STATE(s_schabbchase1)
#endif
#if (!defined(SPEAR))
STATE(s_schabbchase1s)
#endif
#if (!defined(SPEAR))
STATE(s_schabbchase2)
#endif
#if (!defined(SPEAR))
STATE(s_schabbchase3)
#endif
#if (!defined(SPEAR))
STATE(s_schabbchase3s)
#endif
#if (!defined(SPEAR))
STATE(s_schabbchase4)
#endif
#if (!defined(SPEAR))
STATE(s_schabbdeathcam)
#endif
#if (!defined(SPEAR))
STATE(s_schabbdie1)
#endif
#if (!defined(SPEAR))
STATE(s_schabbdie2)
#endif
#if (!defined(SPEAR))
STATE(s_schabbdie3)
#endif
#if (!defined(SPEAR))
STATE(s_schabbdie4)
#endif
#if (!defined(SPEAR))
STATE(s_schabbdie5)
#endif
#if (!defined(SPEAR))
STATE(s_schabbdie6)
#endif
#if (!defined(SPEAR))
STATE(s_schabbshoot1)
#endif
#if (!defined(SPEAR))
STATE(s_schabbshoot2)
#endif
#if (!defined(SPEAR))
STATE(s_needle1)
#endif
#if (!defined(SPEAR))
STATE(s_needle2)
#endif
#if (!defined(SPEAR))
STATE(s_needle3)
#endif
#if (!defined(SPEAR))
STATE(s_needle4)
#endif
#if (!defined(SPEAR))
STATE(s_giftstand)
#endif
#if (!defined(SPEAR))
STATE(s_giftchase1)
#endif
#if (!defined(SPEAR))
STATE(s_giftchase1s)
#endif
#if (!defined(SPEAR))
STATE(s_giftchase2)
#endif
#if (!defined(SPEAR))
STATE(s_giftchase3)
#endif
#if (!defined(SPEAR))
STATE(s_giftchase3s)
#endif
#if (!defined(SPEAR))
STATE(s_giftchase4)
#endif
#if (!defined(SPEAR))
STATE(s_giftdeathcam)
#endif
#if (!defined(SPEAR))
STATE(s_giftdie1)
#endif
#if (!defined(SPEAR))
STATE(s_giftdie2)
#endif
#if (!defined(SPEAR))
STATE(s_giftdie3)
#endif
#if (!defined(SPEAR))
STATE(s_giftdie4)
#endif
#if (!defined(SPEAR))
STATE(s_giftdie5)
#endif
#if (!defined(SPEAR))
STATE(s_giftdie6)
#endif
#if (!defined(SPEAR))
STATE(s_giftshoot1)
#endif
#if (!defined(SPEAR))
STATE(s_giftshoot2)
#endif
#if (!defined(SPEAR))
STATE(s_fatstand)
#endif
#if (!defined(SPEAR))
STATE(s_fatchase1)
#endif
#if (!defined(SPEAR))
STATE(s_fatchase1s)
#endif
#if (!defined(SPEAR))
STATE(s_fatchase2)
#endif
#if (!defined(SPEAR))
STATE(s_fatchase3)
#endif
#if (!defined(SPEAR))
STATE(s_fatchase3s)
#endif
#if (!defined(SPEAR))
STATE(s_fatchase4)
#endif
#if (!defined(SPEAR))
STATE(s_fatdeathcam)
#endif
#if (!defined(SPEAR))
STATE(s_fatdie1)
#endif
#if (!defined(SPEAR))
STATE(s_fatdie2)
#endif
#if (!defined(SPEAR))
STATE(s_fatdie3)
#endif
#if (!defined(SPEAR))
STATE(s_fatdie4)
#endif
#if (!defined(SPEAR))
STATE(s_fatdie5)
#endif
#if (!defined(SPEAR))
STATE(s_fatdie6)
#endif
#if (!defined(SPEAR))
STATE(s_fatshoot1)
#endif
#if (!defined(SPEAR))
STATE(s_fatshoot2)
#endif
#if (!defined(SPEAR))
STATE(s_fatshoot3)
#endif
#if (!defined(SPEAR))
STATE(s_fatshoot4)
#endif
#if (!defined(SPEAR))
STATE(s_fatshoot5)
#endif
#if (!defined(SPEAR))
STATE(s_fatshoot6)
#endif
#if (!defined(SPEAR))
STATE(s_fakestand)
#endif
#if (!defined(SPEAR))
STATE(s_fakechase1)
#endif
#if (!defined(SPEAR))
STATE(s_fakechase1s)
#endif
#if (!defined(SPEAR))
STATE(s_fakechase2)
#endif
#if (!defined(SPEAR))
STATE(s_fakechase3)
#endif
#if (!defined(SPEAR))
STATE(s_fakechase3s)
#endif
#if (!defined(SPEAR))
STATE(s_fakechase4)
#endif
#if (!defined(SPEAR))
STATE(s_fakedie1)
#endif
#if (!defined(SPEAR))
STATE(s_fakedie2)
#endif
#if (!defined(SPEAR))
STATE(s_fakedie3)
#endif
#if (!defined(SPEAR))
STATE(s_fakedie4)
#endif
#if (!defined(SPEAR))
STATE(s_fakedie5)
#endif
#if (!defined(SPEAR))
STATE(s_fakedie6)
#endif
#if (!defined(SPEAR))
STATE(s_fakeshoot1)
#endif
#if (!defined(SPEAR))
STATE(s_fakeshoot2)
#endif
#if (!defined(SPEAR))
STATE(s_fakeshoot3)
#endif
#if (!defined(SPEAR))
STATE(s_fakeshoot4)
#endif
#if (!defined(SPEAR))
STATE(s_fakeshoot5)
#endif
#if (!defined(SPEAR))
STATE(s_fakeshoot6)
#endif
#if (!defined(SPEAR))
STATE(s_fakeshoot7)
#endif
#if (!defined(SPEAR))
STATE(s_fakeshoot8)
#endif
#if (!defined(SPEAR))
STATE(s_fakeshoot9)
#endif
#if (!defined(SPEAR))
STATE(s_fire1)
#endif
#if (!defined(SPEAR))
STATE(s_fire2)
#endif
#if (!defined(SPEAR))
STATE(s_mechastand)
#endif
#if (!defined(SPEAR))
STATE(s_mechachase1)
#endif
#if (!defined(SPEAR))
STATE(s_mechachase1s)
#endif
#if (!defined(SPEAR))
STATE(s_mechachase2)
#endif
#if (!defined(SPEAR))
STATE(s_mechachase3)
#endif
#if (!defined(SPEAR))
STATE(s_mechachase3s)
#endif
#if (!defined(SPEAR))
STATE(s_mechachase4)
#endif
#if (!defined(SPEAR))
STATE(s_mechadie1)
#endif
#if (!defined(SPEAR))
STATE(s_mechadie2)
#endif
#if (!defined(SPEAR))
STATE(s_mechadie3)
#endif
#if (!defined(SPEAR))
STATE(s_mechadie4)
#endif
#if (!defined(SPEAR))
STATE(s_mechashoot1)
#endif
#if (!defined(SPEAR))
STATE(s_mechashoot2)
#endif
#if (!defined(SPEAR))
STATE(s_mechashoot3)
#endif
#if (!defined(SPEAR))
STATE(s_mechashoot4)
#endif
#if (!defined(SPEAR))
STATE(s_mechashoot5)
#endif
#if (!defined(SPEAR))
STATE(s_mechashoot6)
#endif
#if (!defined(SPEAR))
STATE(s_hitlerchase1)
#endif
#if (!defined(SPEAR))
STATE(s_hitlerchase1s)
#endif
#if (!defined(SPEAR))
STATE(s_hitlerchase2)
#endif
#if (!defined(SPEAR))
STATE(s_hitlerchase3)
#endif
#if (!defined(SPEAR))
STATE(s_hitlerchase3s)
#endif
#if (!defined(SPEAR))
STATE(s_hitlerchase4)
#endif
#if (!defined(SPEAR))
STATE(s_hitlerdeathcam)
#endif
#if (!defined(SPEAR))
STATE(s_hitlerdie1)
#endif
#if (!defined(SPEAR))
STATE(s_hitlerdie2)
#endif
#if (!defined(SPEAR))
STATE(s_hitlerdie3)
#endif
#if (!defined(SPEAR))
STATE(s_hitlerdie4)
#endif
#if (!defined(SPEAR))
STATE(s_hitlerdie5)
#endif
#if (!defined(SPEAR))
STATE(s_hitlerdie6)
#endif
#if (!defined(SPEAR))
STATE(s_hitlerdie7)
#endif
#if (!defined(SPEAR))
STATE(s_hitlerdie8)
#endif
#if (!defined(SPEAR))
STATE(s_hitlerdie9)
#endif
#if (!defined(SPEAR))
STATE(s_hitlerdie10)
#endif
#if (!defined(SPEAR))
STATE(s_hitlershoot1)
#endif
#if (!defined(SPEAR))
STATE(s_hitlershoot2)
#endif
#if (!defined(SPEAR))
STATE(s_hitlershoot3)
#endif
#if (!defined(SPEAR))
STATE(s_hitlershoot4)
#endif
#if (!defined(SPEAR))
STATE(s_hitlershoot5)
#endif
#if (!defined(SPEAR))
STATE(s_hitlershoot6)
#endif
#if (!defined(SPEAR))
STATE(s_bjrun1)
#endif
#if (!defined(SPEAR))
STATE(s_bjrun1s)
#endif
#if (!defined(SPEAR))
STATE(s_bjrun2)
#endif
#if (!defined(SPEAR))
STATE(s_bjrun3)
#endif
#if (!defined(SPEAR))
STATE(s_bjrun3s)
#endif
#if (!defined(SPEAR))
STATE(s_bjrun4)
#endif
#if (!defined(SPEAR))
STATE(s_bjjump1)
#endif
#if (!defined(SPEAR))
STATE(s_bjjump2)
#endif
#if (!defined(SPEAR))
STATE(s_bjjump3)
#endif
#if (!defined(SPEAR))
STATE(s_bjjump4)
#endif
#if (!defined(SPEAR))
STATE(s_deathcam)
#endif
STATE(s_netdead)
