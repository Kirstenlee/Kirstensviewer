# -*- cmake -*-

project(viewer)

include(00-Common)
# DON'T move Linking.cmake to its place in the alphabetized list below: it
# sets variables on which the 3p .cmake files depend.
include(Linking)

include(Boost)
include(BuildVersion)
include(CMakeCopyIfDifferent)


include(DragDrop)
include(Expat)
include(FMODSTUDIO)
include(Hunspell)

include(LLAppearance)
include(LLAudio)
include(LLCA)
include(LLCharacter)
include(LLCommon)
include(LLconvexdecomposition)
include(LLCoreHttp)
include(LLImage)
include(LLImageJ2COJ)
include(LLInventory)
include(LLLogin)
include(LLMath)
include(LLMeshOptimizer)
include(LLMessage)
include(LLPlugin)
include(LLPrimitive)
include(LLRender)
include(DxRender)
include(LLUI)
include(LLFileSystem)
include(LLWindow)
include(LLXML)
include(Ndof)
include(NvApi)
include(OpenAL)
include(OpenGL)
include(OpenCL)
include(OpenSSL)
include(OpenXR)
include(Png)
# KV S24: PowerShell-only build system - Python removed!
include(TemplateCheckModern)
include(TinyEXR)
include(Ui)
include(WebRTC)
include(ViewerMiscLibs)

include(ZLib)
include(LLPrimitive)

use_prebuilt_binary(libraries-common)
use_prebuilt_binary(dictionaries)

if(FMODSTUDIO)
  include_directories(${FMODSTUDIO_INCLUDE_DIR})
endif()

include_directories(
    ${JSONCPP_INCLUDE_DIR}
    ${GLOD_INCLUDE_DIR}
    ${LLCHARACTER_INCLUDE_DIRS}
    ${LLCOMMON_INCLUDE_DIRS}
    ${LLCOREHTTP_INCLUDE_DIRS}
    ${LLPHYSICS_INCLUDE_DIRS}
    ${LLIMAGE_INCLUDE_DIRS}
    ${LLINVENTORY_INCLUDE_DIRS}
    ${LLMATH_INCLUDE_DIRS}
    ${LLMESHOPTIMIZER_INCLUDE_DIRS}
    ${LLCONVEXDECOMPOSITION_INCLUDE_DIRS}    # <-- added
    ${DXRENDER_INCLUDE_DIRS} # <-- added framework work in progress
    ${LLMESSAGE_INCLUDE_DIRS}
    ${LLPLUGIN_INCLUDE_DIRS}
    ${LLPRIMITIVE_INCLUDE_DIRS}
    ${LLRENDER_INCLUDE_DIRS}
    ${LLUI_INCLUDE_DIRS}
    ${LLFILESYSTEM_INCLUDE_DIRS}
    ${LLWINDOW_INCLUDE_DIRS}
    ${LLXML_INCLUDE_DIRS}
    ${LLLOGIN_INCLUDE_DIRS}
    ${LIBS_PREBUILT_DIR}/include/tinygltf
    ${LIBS_PREBUILT_DIR}/include/collada
    ${LIBS_PREBUILD_DIR}/include/hunspell
    ${LIBS_PREBUILD_DIR}/include/webrtc
    ${OPENAL_LIB_INCLUDE_DIRS}
    ${LIBS_PREBUILT_DIR}/include/collada/1.4
    ${LLAPPEARANCE_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    )

include_directories(SYSTEM
    ${LLCOMMON_SYSTEM_INCLUDE_DIRS}
    ${LLXML_SYSTEM_INCLUDE_DIRS}
    ${LLPHYSICSEXTENSIONS_INCLUDE_DIRS}
    )

set(viewer_SOURCE_FILES
    ao.cpp
    aoengine.cpp
    aoset.cpp
    gltfscenemanager.cpp
    gltf/asset.cpp
    gltf/accessor.cpp
    gltf/primitive.cpp
    gltf/animation.cpp
    gltf/llgltfloader.cpp
    groupchatlistener.cpp
    kveffects.cpp
    kvopencl.cpp
    kvramcache.cpp
    kvcachethread.cpp
    llaccountingcostmanager.cpp
    llaisapi.cpp
    llagent.cpp
    llagentaccess.cpp
    llagentbenefits.cpp
    llagentcamera.cpp
    llagentdata.cpp
    llagentlanguage.cpp
    llagentlistener.cpp
    llagentpicksinfo.cpp
    llagentpilot.cpp
    llagentui.cpp
    llagentwearables.cpp
    llanimstatelabels.cpp
    llappcorehttp.cpp
    llappearancelistener.cpp
    llappearancemgr.cpp
    llappviewer.cpp
    llappviewerlistener.cpp
    llattachmentsmgr.cpp
    llaudiosourcevo.cpp
    llautoreplace.cpp
    llavataractions.cpp
    llavatariconctrl.cpp
    llavatarlist.cpp
    llavatarlistitem.cpp
    llavatarrenderinfoaccountant.cpp
    llavatarrendernotifier.cpp
    llavatarpropertiesprocessor.cpp
    llblockedlistitem.cpp
    llblocklist.cpp
    llbox.cpp
    llbrowsernotification.cpp
    llbuycurrencyhtml.cpp
    llcallingcard.cpp
    llchannelmanager.cpp
    llchatbar.cpp
    llchathistory.cpp
    llchatitemscontainerctrl.cpp
    llchatmsgbox.cpp
    llchiclet.cpp
    llchicletbar.cpp
    llclassifiedinfo.cpp
    llcofwearables.cpp
    llcolorswatch.cpp
    llcommanddispatcherlistener.cpp
    llcommandhandler.cpp
    llcommandlineparser.cpp
    llcommunicationchannel.cpp
    llcompilequeue.cpp
    llconfirmationmanager.cpp
    llcontrolavatar.cpp
    llconversationlog.cpp
    llconversationloglist.cpp
    llconversationloglistitem.cpp
    llconversationmodel.cpp
    llconversationview.cpp
    llcurrencyuimanager.cpp
    llcylinder.cpp
    lldateutil.cpp
    lldebugmessagebox.cpp
    lldebugview.cpp
    lldeferredsounds.cpp
    lldelayedgestureerror.cpp
    lldirpicker.cpp
    lldonotdisturbnotificationstorage.cpp
    lldndbutton.cpp
    lldrawable.cpp
    lldrawpool.cpp
    lldrawpoolalpha.cpp
    lldrawpoolavatar.cpp
    lldrawpoolbump.cpp
    lldrawpoolmaterials.cpp
    lldrawpoolpbropaque.cpp
    lldrawpoolsimple.cpp
    lldrawpoolsky.cpp
    lldrawpoolterrain.cpp
    lldrawpooltree.cpp
    lldrawpoolwater.cpp
    lldrawpoolwlsky.cpp
    lldrawpoolwaterexclusion.cpp
    lldynamictexture.cpp
    llemote.cpp
    llenvironment.cpp
    llestateinfomodel.cpp
    lleventnotifier.cpp
    lleventpoll.cpp
    llexpandabletextbox.cpp
    llexperiencelog.cpp
    llexternaleditor.cpp
    llface.cpp
    llfasttimerview.cpp
    llfavoritesbar.cpp
    llfeaturemanager.cpp
    llfetchedgltfmaterial.cpp
    llfilepicker.cpp
    llfilteredwearablelist.cpp
    llfirstuse.cpp
    llflexibleobject.cpp
    llfloater360capture.cpp
    llfloaterabout.cpp
    llfloateravatarwelcomepack.cpp
    llfloaterbvhpreview.cpp
    llfloateraddpaymentmethod.cpp
    llfloaterauction.cpp
    llfloaterautoreplacesettings.cpp
    llfloateravatarpicker.cpp
    llfloateravatarrendersettings.cpp
    llfloateravatartextures.cpp
    llfloaterbanduration.cpp
    llfloaterbeacons.cpp
    llfloaterbuildoptions.cpp
    llfloaterbulkpermission.cpp
    llfloaterbulkupload.cpp
    llfloaterbump.cpp
    llfloaterbuy.cpp
    llfloaterbuycontents.cpp
    llfloaterbuycurrency.cpp
    llfloaterbuycurrencyhtml.cpp
    llfloaterbuyland.cpp
    llfloatercamera.cpp
    llfloatercamerapresets.cpp
    llfloaterchangeitemthumbnail.cpp
    llfloaterchatmentionpicker.cpp
    llfloaterchatvoicevolume.cpp
    llfloaterclassified.cpp
    llfloatercolorpicker.cpp
    llfloaterconversationlog.cpp
    llfloaterconversationpreview.cpp
    llfloatercreatelandmark.cpp
    llfloaterdeleteprefpreset.cpp
    llfloaterdestinations.cpp
    llfloaterdirectory.cpp
    llfloaterdisplayname.cpp
    llfloatereditenvironmentbase.cpp
    llfloatereditextdaycycle.cpp
    llfloateremojipicker.cpp
    llfloaterenvironmentadjust.cpp
    llfloaterevent.cpp
    llfloaterexperiencepicker.cpp
    llfloaterexperienceprofile.cpp
    llfloaterexperiences.cpp
    llfloaterfixedenvironment.cpp
    llfloaterfonttest.cpp
    llfloaterforgetuser.cpp
    llfloatergesture.cpp
    llfloatergestureautocompletepicker.cpp
    llfloatergltfasseteditor.cpp
    llfloatergodtools.cpp
    llfloatergotoline.cpp
    llfloatergridstatus.cpp
    llfloatergroupbulkban.cpp
    llfloatergroupinvite.cpp
    llfloatergroups.cpp
    llfloaterhandler.cpp
    llfloaterhelpbrowser.cpp
    llfloaterhoverheight.cpp
    llfloaterhud.cpp
    llfloaterimagepreview.cpp
    llfloaterimsessiontab.cpp
    llfloaterimsession.cpp
    llfloaterimcontainer.cpp
    llfloaterinspect.cpp
    llfloaterinventorysettings.cpp
    llfloaterinventorythumbnailshelper.cpp
    llfloaterjoystick.cpp
    llfloaterlagmeter.cpp
    llfloaterland.cpp
    llfloaterlandholdings.cpp
    llfloaterlinkreplace.cpp
    llfloaterloadprefpreset.cpp
    llfloaterKVTweaks.cpp
    llfloaterkvramcachestats.cpp
    llfloatermarketplacelistings.cpp
    llfloatermap.cpp
    llfloatermediasettings.cpp
    llfloatermemleak.cpp
    llfloatermodelpreview.cpp
    llfloatermodeluploadbase.cpp
    llfloatermyscripts.cpp
    llfloatermyenvironment.cpp
    llfloaternamedesc.cpp
    llfloaternewfeaturenotification.cpp
    llfloaternotificationsconsole.cpp
    llfloaternotificationstabbed.cpp
    llfloaterobjectweights.cpp
    llfloateropenobject.cpp
    llfloatersimplesnapshot.cpp
    llfloaterpathfindingcharacters.cpp
    llfloaterpathfindingconsole.cpp
    llfloaterpathfindinglinksets.cpp
    llfloaterpathfindingobjects.cpp
    llfloaterpay.cpp
    llfloaterperformance.cpp
    llfloaterperms.cpp
    llfloaterprofile.cpp
    llfloaterpreference.cpp
    llfloaterpreferencesgraphicsadvanced.cpp
    llfloaterpreferenceviewadvanced.cpp
    llfloaterpreviewtrash.cpp
    llfloaterprofiletexture.cpp
    kvfloaterquickchat.cpp
    kvdebugconsole.cpp
    llfloaterregiondebugconsole.cpp
    llfloaterregioninfo.cpp
    llfloaterreporter.cpp
    llfloaterregionrestarting.cpp
    llfloaterregionrestartschedule.cpp
    llfloatersavecamerapreset.cpp
    llfloatersaveprefpreset.cpp
    llfloatersceneloadstats.cpp
    llfloaterscriptdebug.cpp
    llfloaterscriptedprefs.cpp
    llfloaterscriptlimits.cpp
    llfloatersearch.cpp
    llfloatersellland.cpp
    llfloatersettingscolor.cpp
    llfloatersettingsdebug.cpp
    llfloatersidepanelcontainer.cpp
    llfloaterslapptest.cpp
    llfloatersnapshot.cpp
    llfloaterspellchecksettings.cpp
    llfloatertelehub.cpp
    llfloatertestinspectors.cpp
    llfloatertestlistview.cpp
    llfloatertools.cpp
    llfloatertopobjects.cpp
    llfloatertos.cpp
    llfloatertoybox.cpp
    llfloatertranslationsettings.cpp
    llfloateruipreview.cpp
    llfloaterurlentry.cpp
    llfloatervoicevolume.cpp
    llfloaterwebcontent.cpp
    llfloaterwhitelistentry.cpp
    llfloaterwindowsize.cpp
    llfloaterworldmap.cpp
    llfolderviewmodelinventory.cpp
    llfollowcam.cpp
    llfriendcard.cpp
    llflyoutcombobtn.cpp
    llgesturelistener.cpp
    llgesturemgr.cpp
    llgiveinventory.cpp
    llglsandbox.cpp
    llgltffolderitem.cpp
    llgltffoldermodel.cpp
    llgltfmateriallist.cpp
    llgltfmaterialpreviewmgr.cpp
    llgroupactions.cpp
    llgroupchatmute.cpp
    llgroupiconctrl.cpp
    llgrouplist.cpp
    llgroupmgr.cpp
    llhasheduniqueid.cpp
    llhints.cpp
    llhttpretrypolicy.cpp
    llhudeffect.cpp
    llhudeffectbeam.cpp
    llhudeffectlookat.cpp
    llhudeffectpointat.cpp
    llhudeffecttrail.cpp
    llhudeffectblob.cpp
    llhudeffectresetskeleton.cpp
    llhudicon.cpp
    llhudmanager.cpp
    llhudnametag.cpp
    llhudobject.cpp
    llhudrender.cpp
    llhudtext.cpp
    llhudview.cpp
    llimagefiltersmanager.cpp
    llimhandler.cpp
    llimprocessing.cpp
    llimview.cpp
    llinspect.cpp
    llinspectavatar.cpp
    llinspectgroup.cpp
    llinspectobject.cpp
    llinspectremoteobject.cpp
    llinspecttexture.cpp
    llinspecttoast.cpp
    llinventorybridge.cpp
    llinventoryfilter.cpp
    llinventoryfunctions.cpp
    llinventorygallery.cpp
    llinventorygallerymenu.cpp
    llinventoryicon.cpp
    llinventoryitemslist.cpp
    llinventorylistener.cpp
    llinventorylistitem.cpp
    llinventorymodel.cpp
    llinventorymodelbackgroundfetch.cpp
    llinventoryobserver.cpp
    llinventorypanel.cpp
    lljoystickbutton.cpp
    llkeyconflict.cpp
    lllandmarkactions.cpp
    lllandmarklist.cpp
    lllegacyatmospherics.cpp
    lllistcontextmenu.cpp
    lllistview.cpp
    lllocalbitmaps.cpp
    lllocalgltfmaterials.cpp
    lllocationhistory.cpp
    lllocationinputctrl.cpp
    lllogchat.cpp
    llloginhandler.cpp
    lllogininstance.cpp
    llmachineid.cpp
    llmanip.cpp
    llmaniprotate.cpp
    llmanipscale.cpp
    llmaniptranslate.cpp
    llfloatermarketplace.cpp
    llmarketplacefunctions.cpp
    llmarketplacenotifications.cpp
    llmaterialeditor.cpp
    llmaterialmgr.cpp
    llmediactrl.cpp
    llmediadataclient.cpp
    llmenuoptionpathfindingrebakenavmesh.cpp
    llmeshrepository.cpp
    llmimetypes.cpp
    llmodelpreview.cpp
    llmorphview.cpp
    llmoveview.cpp
    llmutelist.cpp
    llnamebox.cpp
    llnameeditor.cpp
    llnamelistctrl.cpp
    llnavigationbar.cpp
    llfloaterimnearbychat.cpp
    llfloaterimnearbychathandler.cpp
    llfloaterimnearbychatlistener.cpp
    llnearbyvoicemoderation.cpp
    llnetmap.cpp
    llnotificationalerthandler.cpp
    llnotificationgrouphandler.cpp
    llnotificationhandlerutil.cpp
    llnotificationhinthandler.cpp
    llnotificationlistitem.cpp
    llnotificationlistview.cpp
    llnotificationmanager.cpp
    llnotificationofferhandler.cpp
    llnotificationscripthandler.cpp
    llnotificationstorage.cpp
    llnotificationtiphandler.cpp
    lloutfitgallery.cpp
    lloutfitslist.cpp
    lloutfitobserver.cpp
    lloutputmonitorctrl.cpp
    llpanelappearancetab.cpp
    llpanelavatar.cpp
    llpanelavatartag.cpp
    llpanelblockedlist.cpp
    llpanelclassified.cpp
    llpanelcontents.cpp
    llpaneldirbrowser.cpp
    llpaneldirclassified.cpp
    llpaneldirevents.cpp
    llpaneldirgroups.cpp
    llpaneldirland.cpp
    llpaneldirpeople.cpp
    llpaneldirplaces.cpp
    llpaneldirweb.cpp
    llpaneleditsky.cpp
    llpaneleditwater.cpp
    llpaneleditwearable.cpp
    llpanelemojicomplete.cpp
    llpanelenvironment.cpp
    llpaneleventinfo.cpp
    llpanelexperiencelisteditor.cpp
    llpanelexperiencelog.cpp
    llpanelexperiencepicker.cpp
    llpanelexperiences.cpp
    llpanelface.cpp
    llpanelgenerictip.cpp
    llpanelgroup.cpp
    llpanelgroupcreate.cpp
    llpanelgroupbulk.cpp
    llpanelgroupbulkban.cpp
    llpanelgroupexperiences.cpp
    llpanelgroupgeneral.cpp
    llpanelgroupinvite.cpp
    llpanelgrouplandmoney.cpp
    llpanelgroupnotices.cpp
    llpanelgrouproles.cpp
    llpanelhome.cpp
    llpanelland.cpp
    llpanellandaudio.cpp
    llpanellandmarkinfo.cpp
    llpanellandmarks.cpp
    llpanellandmedia.cpp
    llpanellogin.cpp
    llpanelloginlistener.cpp
    llpanelmaininventory.cpp
    llpanelmarketplaceinbox.cpp
    llpanelmarketplaceinboxinventory.cpp
    llpanelmediasettingsgeneral.cpp
    llpanelmediasettingspermissions.cpp
    llpanelmediasettingssecurity.cpp
    llpanelnearbymedia.cpp
    llpanelobject.cpp
    llpanelobjectinventory.cpp
    llpanelonlinestatus.cpp
    llpaneloutfitedit.cpp
    llpaneloutfitsinventory.cpp
    llpanelpeople.cpp
    llpanelpeoplemenus.cpp
    llpanelpermissions.cpp
    llpanelplaceinfo.cpp
    llpanelplaceprofile.cpp
    llpanelplaces.cpp
    llpanelplacestab.cpp
    llpanelpresetscamerapulldown.cpp
    llpanelpresetspulldown.cpp
    llpanelprimmediacontrols.cpp
    llpanelprofile.cpp
    llpanelprofileclassifieds.cpp
    kvpanelprofilemetadata.cpp
    llpanelprofilepicks.cpp
    llpanelsnapshot.cpp
    llpanelsnapshotinventory.cpp
    llpanelsnapshotlocal.cpp
    llpanelsnapshotoptions.cpp
    llpanelsnapshotpostcard.cpp
    llpanelsnapshotprofile.cpp
    llpanelteleporthistory.cpp
    llpaneltiptoast.cpp
    llpaneltopinfobar.cpp
    llpanelpulldown.cpp
    llpanelvoicedevicesettings.cpp
    llpanelvolume.cpp
    llpanelvolumepulldown.cpp
    llpanelwearing.cpp
    llparcelselection.cpp
    llparticipantlist.cpp
    llpatchvertexarray.cpp
    llpathfindingcharacter.cpp
    llpathfindingcharacterlist.cpp
    llpathfindinglinkset.cpp
    llpathfindinglinksetlist.cpp
    llpathfindingmanager.cpp
    llpathfindingnavmesh.cpp
    llpathfindingnavmeshstatus.cpp
    llpathfindingnavmeshzone.cpp
    llpathfindingobject.cpp
    llpathfindingobjectlist.cpp
    llpathfindingpathtool.cpp
    llpbrterrainfeatures.cpp
    llpersistentnotificationstorage.cpp
    llphysicsmotion.cpp
    llphysicsshapebuilderutil.cpp
    llpipelinelistener.cpp
    llplacesinventorybridge.cpp
    llplacesinventorypanel.cpp
    llplacesfolderview.cpp
    llpopupview.cpp
    llpostcard.cpp
    llpresetsmanager.cpp
    llpreview.cpp
    llpreviewanim.cpp
    llpreviewgesture.cpp
    llpreviewnotecard.cpp
    llpreviewscript.cpp
    llpreviewsound.cpp
    llpreviewtexture.cpp
    llproductinforequest.cpp
    llprogressview.cpp
    llrecentpeople.cpp
    llreflectionmap.cpp
    llreflectionmapmanager.cpp
    llheroprobemanager.cpp
    llregioninfomodel.cpp
    llregionposition.cpp
    llremoteparcelrequest.cpp
    llsaveoutfitcombobtn.cpp
    llscenemonitor.cpp
    llsceneview.cpp
    llscreenchannel.cpp
    llscripteditor.cpp
    llscriptfloater.cpp
    llscrollingpanelparam.cpp
    llscrollingpanelparambase.cpp
    llsculptidsize.cpp
    llsearchableui.cpp
    llsearchcombobox.cpp
    llsearchhistory.cpp
    llsecapi.cpp
    llsechandler_basic.cpp
    llselectmgr.cpp
    llsetkeybinddialog.cpp
    llsettingspicker.cpp
    llsettingsvo.cpp
    llshareavatarhandler.cpp
    llsidepanelappearance.cpp
    llsidepanelinventory.cpp
    llsidepanelinventorysubpanel.cpp
    llsidepaneliteminfo.cpp
    llsidepaneltaskinfo.cpp
    llsidetraypanelcontainer.cpp
    llskinningutil.cpp
    llsky.cpp
    llslurl.cpp
    llsnapshotlivepreview.cpp
    llspatialpartition.cpp
    llspeakers.cpp
    llspeakingindicatormanager.cpp
    llsplitbutton.cpp
    llsprite.cpp
    llstartup.cpp
    llstartuplistener.cpp
    llstatusbar.cpp
    llstylemap.cpp
    llsurface.cpp
    llsurfacepatch.cpp
    llsyntaxid.cpp
    llsyswellitem.cpp
    llsyswellwindow.cpp
    llteleporthistory.cpp
    llteleporthistorystorage.cpp
    llterrainpaintmap.cpp
    lltexturecache.cpp
    lltexturectrl.cpp
    lltexturefetch.cpp
    lltextureinfo.cpp
    lltextureinfodetails.cpp
    lltexturestats.cpp
    lltextureview.cpp
    llthumbnailctrl.cpp
    lltinygltfhelper.cpp
    lltoast.cpp
    lltoastalertpanel.cpp
    lltoastgroupnotifypanel.cpp
    lltoastimpanel.cpp
    lltoastnotifypanel.cpp
    lltoastpanel.cpp
    lltoastscripttextbox.cpp
    lltoastscriptquestion.cpp
    lltool.cpp
    lltoolbarview.cpp
    lltoolbrush.cpp
    lltoolcomp.cpp
    lltooldraganddrop.cpp
    lltoolface.cpp
    lltoolfocus.cpp
    lltoolgrab.cpp
    lltoolgun.cpp
    lltoolindividual.cpp
    lltoolmgr.cpp
    lltoolmorph.cpp
    lltoolobjpicker.cpp
    lltoolpie.cpp
    lltoolpipette.cpp
    lltoolplacer.cpp
    lltoolselect.cpp
    lltoolselectland.cpp
    lltoolselectrect.cpp
    lltracker.cpp
    lltrackpicker.cpp
    lltransientdockablefloater.cpp
    lltransientfloatermgr.cpp
    lltranslate.cpp
    lluiavatar.cpp
    lluilistener.cpp
    lluploaddialog.cpp
    llurl.cpp
    llurldispatcher.cpp
    llurldispatcherlistener.cpp
    llurlfloaterdispatchhandler.cpp
    llurlhistory.cpp
    llurllineeditorctrl.cpp
    llurlwhitelist.cpp
    llversioninfo.cpp
    llviewchildren.cpp
    llviewerassetstats.cpp
    llviewerassetstorage.cpp
    llviewerassettype.cpp
    llviewerassetupload.cpp
    llviewerattachmenu.cpp
    llvieweraudio.cpp
    llviewercamera.cpp
    llviewerchat.cpp
    llviewercontrol.cpp
    llviewercontrollistener.cpp
    llviewerdisplay.cpp
    llviewerdisplayname.cpp
    llviewerfloaterreg.cpp
    llviewerfoldertype.cpp
    llviewergenericmessage.cpp
    llviewergesture.cpp
    llviewerhelp.cpp
    llviewerhelputil.cpp
    llviewerhome.cpp
    llviewerinventory.cpp
    llviewerjoint.cpp
    llviewerjointattachment.cpp
    llviewerjointmesh.cpp
    llviewerjoystick.cpp
    llviewerinput.cpp
    llviewerlayer.cpp
    llviewermedia.cpp
    llviewermedia_streamingaudio.cpp
    llviewermediafocus.cpp
    llviewermenu.cpp
    llviewermenufile.cpp
    llviewermessage.cpp
    llviewernetwork.cpp
    llviewerobject.cpp
    llviewerobjectlist.cpp
    llvieweroctree.cpp
    llviewerparcelaskplay.cpp
    llviewerparcelmedia.cpp
    llviewerparcelmediaautoplay.cpp
    llviewerparcelmgr.cpp
    llviewerparceloverlay.cpp
    llviewerpartsim.cpp
    llviewerpartsource.cpp
    llviewerregion.cpp
    llviewershadermgr.cpp
    llviewerstats.cpp
    llviewerstatsrecorder.cpp
    llviewertexlayer.cpp
    llviewertexteditor.cpp
    llviewertexture.cpp
    llviewertextureanim.cpp
    llviewertexturelist.cpp
    llviewerthrottle.cpp
    llviewerwearable.cpp
    llviewerwindow.cpp
    llviewerwindowlistener.cpp
    llvlcomposition.cpp
    llvlmanager.cpp
    llvoavatar.cpp
    llvoavatarself.cpp
    llvocache.cpp
    llvograss.cpp
    llvoicecallhandler.cpp
    llvoicechannel.cpp
    llvoiceclient.cpp
    llvoicevisualizer.cpp
#   llvoicevivox.cpp  # S24: Vivox removed - WebRTC only
    llvoicewebrtc.cpp
    llvoinventorylistener.cpp
    llvopartgroup.cpp
    llvosky.cpp
    llvosurfacepatch.cpp
    llvotree.cpp
    llvovolume.cpp
    llvowater.cpp
    llvowlsky.cpp
    llwatchdog.cpp
    llwearableitemslist.cpp
    llwearablelist.cpp
    llweb.cpp
    llwebprofile.cpp
    llwind.cpp
    llwindowlistener.cpp
    llwlhandlers.cpp
    llworld.cpp
    llworldmap.cpp
    llworldmapmessage.cpp
    llworldmipmap.cpp
    llworldmapview.cpp
    llxmlrpclistener.cpp
    llxmlrpctransaction.cpp
    noise.cpp
    pipeline.cpp
    )

set(VIEWER_BINARY_NAME "Kirstens-S24" CACHE STRING
    "The name of the viewer executable to create.")

set(viewer_HEADER_FILES
    CMakeLists.txt
    ViewerInstall.cmake
    ao.h
    aoengine.h
    aoset.h
    gltfscenemanager.h
    groupchatlistener.h
    gltf/asset.h
    gltf/accessor.h
    gltf/buffer_util.h
    gltf/primitive.h
    gltf/animation.h
    gltf/llgltfloader.h
    kveffects.h
    kvopencl.h
    kvramcache.h
    kvcachethread.h
    llaccountingcost.h
    llaccountingcostmanager.h
    llaisapi.h
    llagent.h
    llagentaccess.h
    llagentbenefits.h
    llagentcamera.h
    llagentdata.h
    llagentlanguage.h
    llagentlistener.h
    llagentpicksinfo.h
    llagentpilot.h
    llagentui.h
    llagentwearables.h
    llanimstatelabels.h
    llappcorehttp.h
    llappearance.h
    llappearancelistener.h
    llappearancemgr.h
    llappviewer.h
    llappviewerlistener.h
    llattachmentsmgr.h
    llaudiosourcevo.h
    llautoreplace.h
    llavataractions.h
    llavatariconctrl.h
    llavatarlist.h
    llavatarlistitem.h
    llavatarpropertiesprocessor.h
    llavatarrenderinfoaccountant.h
    llavatarrendernotifier.h
    llblockedlistitem.h
    llblocklist.h
    llbox.h
    llbuycurrencyhtml.h
    llcallingcard.h
    llcapabilityprovider.h
    llchannelmanager.h
    llchatbar.h
    llchathistory.h
    llchatitemscontainerctrl.h
    llchatmsgbox.h
    llchiclet.h
    llchicletbar.h
    llclassifiedinfo.h
    llcofwearables.h
    llcolorswatch.h
    llcommanddispatcherlistener.h
    llcommandhandler.h
    llcommandlineparser.h
    llcommunicationchannel.h
    llcompilequeue.h
    llconfirmationmanager.h
    llcontrolavatar.h
    llconversationlog.h
    llconversationloglist.h
    llconversationloglistitem.h
    llconversationmodel.h
    llconversationview.h
    llcurrencyuimanager.h
    llcylinder.h
    lldateutil.h
    lldebugmessagebox.h
    lldebugview.h
    lldeferredsounds.h
    lldelayedgestureerror.h
    lldirpicker.h
    lldonotdisturbnotificationstorage.h
    lldndbutton.h
    lldrawable.h
    lldrawpool.h
    lldrawpoolalpha.h
    lldrawpoolavatar.h
    lldrawpoolbump.h
    lldrawpoolmaterials.h
    lldrawpoolpbropaque.h
    lldrawpoolsimple.h
    lldrawpoolsky.h
    lldrawpoolterrain.h
    lldrawpooltree.h
    lldrawpoolwater.h
    lldrawpoolwlsky.h
    lldrawpoolwaterexclusion.h
    lldynamictexture.h
    llemote.h
    llenvironment.h
    llestateinfomodel.h
    lleventnotifier.h
    lleventpoll.h
    llexpandabletextbox.h
    llexperiencelog.h
    llexternaleditor.h
    llface.h
    llfasttimerview.h
    llfavoritesbar.h
    llfeaturemanager.h
    llfetchedgltfmaterial.h
    llfilepicker.h
    llfilteredwearablelist.h
    llfirstuse.h
    llflexibleobject.h
    llfloater360capture.h
    llfloaterabout.h
    llfloateravatarwelcomepack.h
    llfloaterbvhpreview.h
    llfloateraddpaymentmethod.h
    llfloaterauction.h
    llfloaterautoreplacesettings.h
    llfloateravatarpicker.h
    llfloateravatarrendersettings.h
    llfloateravatartextures.h
    llfloaterbanduration.h
    llfloaterbeacons.h
    llfloaterbuildoptions.h
    llfloaterbulkpermission.h
    llfloaterbulkupload.h
    llfloaterbump.h
    llfloaterbuy.h
    llfloaterbuycontents.h
    llfloaterbuycurrency.h
    llfloaterbuycurrencyhtml.h
    llfloaterbuyland.h
    llfloatercamerapresets.h
    llfloaterchangeitemthumbnail.h
    llfloaterchatmentionpicker.h
    llfloatercamera.h
    llfloaterchatvoicevolume.h
    llfloaterclassified.h
    llfloatercolorpicker.h
    llfloaterconversationlog.h
    llfloaterconversationpreview.h
    llfloatercreatelandmark.h
    llfloaterdeleteprefpreset.h
    llfloaterdestinations.h
    llfloaterdirectory.h
    llfloaterdisplayname.h
    llfloatereditenvironmentbase.h
    llfloatereditextdaycycle.h
    llfloateremojipicker.h
    llfloaterenvironmentadjust.h
    llfloaterevent.h
    llfloaterexperiencepicker.h
    llfloaterexperienceprofile.h
    llfloaterexperiences.h
    llfloaterfixedenvironment.h
    llfloaterfonttest.h
    llfloaterforgetuser.h
    llfloatergesture.h
    llfloatergestureautocompletepicker.h
    llfloatergltfasseteditor.h
    llfloatergodtools.h
    llfloatergotoline.h
    llfloatergridstatus.h
    llfloatergroupbulkban.h
    llfloatergroupinvite.h
    llfloatergroups.h
    llfloaterhandler.h
    llfloaterhelpbrowser.h
    llfloaterhoverheight.h
    llfloaterhud.h
    llfloaterimagepreview.h
    llfloaterimnearbychat.h
    llfloaterimnearbychathandler.h
    llfloaterimnearbychatlistener.h
    llfloaterimsessiontab.h
    llfloaterimsession.h
    llfloaterimcontainer.h
    llfloaterinspect.h
    llfloaterinventorysettings.h
    llfloaterinventorythumbnailshelper.h
    llfloaterjoystick.h
    llfloaterlagmeter.h
    llfloaterland.h
    llfloaterlandholdings.h
    llfloaterlinkreplace.h
    llfloaterloadprefpreset.h
    llfloaterKVTweaks.h
    llfloaterkvramcachestats.h
    llfloatermap.h
    llfloatermarketplace.h
    llfloatermarketplacelistings.h
    llfloatermediasettings.h
    llfloatermemleak.h
    llfloatermodelpreview.h
    llfloatermodeluploadbase.h
    llfloatermyscripts.h
    llfloatermyenvironment.h
    llfloaternamedesc.h
    llfloaternewfeaturenotification.h
    llfloaternotificationsconsole.h
    llfloaternotificationstabbed.h
    llfloaterobjectweights.h
    llfloateropenobject.h
    llfloatersimplesnapshot.h
    llfloaterpathfindingcharacters.h
    llfloaterpathfindingconsole.h
    llfloaterpathfindinglinksets.h
    llfloaterpathfindingobjects.h
    llfloaterpay.h
    llfloaterperformance.h
    llfloaterperms.h
    llfloaterprofile.h
    llfloaterpreference.h
    llfloaterpreferencesgraphicsadvanced.h
    llfloaterpreferenceviewadvanced.h
    llfloaterpreviewtrash.h
    llfloaterprofiletexture.h
    kvfloaterquickchat.h
    kvdebugconsole.h
    llfloaterregiondebugconsole.h
    llfloaterregioninfo.h
    llfloaterreporter.h
    llfloaterregionrestarting.h
    llfloaterregionrestartschedule.h
    llfloatersavecamerapreset.h
    llfloatersaveprefpreset.h
    llfloatersceneloadstats.h
    llfloaterscriptdebug.h
    llfloaterscriptedprefs.h
    llfloaterscriptlimits.h
    llfloatersearch.h
    llfloatersellland.h
    llfloatersettingscolor.h
    llfloatersettingsdebug.h
    llfloatersidepanelcontainer.h
    llfloaterslapptest.h
    llfloatersnapshot.h
    llfloaterspellchecksettings.h
    llfloatertelehub.h
    llfloatertestinspectors.h
    llfloatertestlistview.h
    llfloatertools.h
    llfloatertopobjects.h
    llfloatertos.h
    llfloatertoybox.h
    llfloatertranslationsettings.h
    llfloateruipreview.h
    llfloaterurlentry.h
    llfloatervoicevolume.h
    llfloaterwebcontent.h
    llfloaterwhitelistentry.h
    llfloaterwindowsize.h
    llfloaterworldmap.h
    llfolderviewmodelinventory.h
    llfollowcam.h
    llfriendcard.h
    llflyoutcombobtn.h
    llgesturelistener.h
    llgesturemgr.h
    llgiveinventory.h
    llgltffolderitem.h
    llgltffoldermodel.h
    llgltfmateriallist.h
    llgltfmaterialpreviewmgr.h
    llgroupactions.h
    llgroupiconctrl.h
    llgrouplist.h
    llgroupmgr.h
    llhasheduniqueid.h
    llhints.h
    llhttpretrypolicy.h
    llhudeffect.h
    llhudeffectbeam.h
    llhudeffectlookat.h
    llhudeffectpointat.h
    llhudeffecttrail.h
    llhudeffectblob.h
    llhudeffectresetskeleton.h
    llhudicon.h
    llhudmanager.h
    llhudnametag.h
    llhudobject.h
    llhudrender.h
    llhudtext.h
    llhudview.h
    llimagefiltersmanager.h
    llimprocessing.h
    llimview.h
    llinspect.h
    llinspectavatar.h
    llinspectgroup.h
    llinspectobject.h
    llinspectremoteobject.h
    llinspecttexture.h
    llinspecttoast.h
    llinventorybridge.h
    llinventoryfilter.h
    llinventoryfunctions.h
    llinventorygallery.h
    llinventorygallerymenu.h
    llinventoryicon.h
    llinventoryitemslist.h
    llinventorylistener.h
    llinventorylistitem.h
    llinventorymodel.h
    llinventorymodelbackgroundfetch.h
    llinventoryobserver.h
    llinventorypanel.h
    lljoystickbutton.h
    llkeyconflict.h
    lllandmarkactions.h
    lllandmarklist.h
    lllightconstants.h
    lllistcontextmenu.h
    lllistview.h
    lllocalbitmaps.h
    lllocalgltfmaterials.h
    lllocationhistory.h
    lllocationinputctrl.h
    lllogchat.h
    llloginhandler.h
    lllogininstance.h
    llmachineid.h
    llmanip.h
    llmaniprotate.h
    llmanipscale.h
    llmaniptranslate.h
    llmarketplacefunctions.h
    llmarketplacenotifications.h
    llmaterialeditor.h
    llmaterialmgr.h
    llmediactrl.h
    llmediadataclient.h
    llmenuoptionpathfindingrebakenavmesh.h
    llmeshrepository.h
    llmimetypes.h
    llmodelpreview.h
    llmorphview.h
    llmoveview.h
    llmutelist.h
    llnamebox.h
    llnameeditor.h
    llnamelistctrl.h
    llnavigationbar.h
    llnearbyvoicemoderation.h
    llnetmap.h
    llnotificationhandler.h
    llnotificationlistitem.h
    llnotificationlistview.h
    llnotificationmanager.h
    llnotificationstorage.h
    lloutfitgallery.h
    lloutfitslist.h
    lloutfitobserver.h
    lloutputmonitorctrl.h
    llpanelappearancetab.h
    llpanelavatar.h
    llpanelavatartag.h
    llpanelblockedlist.h
    llpanelclassified.h
    llpanelcontents.h
    llpaneldirbrowser.h
    llpaneldirclassified.h
    llpaneldirevents.h
    llpaneldirgroups.h
    llpaneldirland.h
    llpaneldirpeople.h
    llpaneldirplaces.h
    llpaneldirweb.h
    llpaneleditsky.h
    llpaneleditwater.h
    llpaneleditwearable.h
    llpanelemojicomplete.h
    llpanelenvironment.h
    llpaneleventinfo.h
    llpanelexperiencelisteditor.h
    llpanelexperiencelog.h
    llpanelexperiencepicker.h
    llpanelexperiences.h
    llpanelface.h
    llpanelgenerictip.h
    llpanelgroup.h
    llpanelgroupcreate.h
    llpanelgroupbulk.h
    llpanelgroupbulkimpl.h
    llpanelgroupbulkban.h
    llpanelgroupexperiences.h
    llpanelgroupgeneral.h
    llpanelgroupinvite.h
    llpanelgrouplandmoney.h
    llpanelgroupnotices.h
    llpanelgrouproles.h
    llpanelhome.h
    llpanelland.h
    llpanellandaudio.h
    llpanellandmarkinfo.h
    llpanellandmarks.h
    llpanellandmedia.h
    llpanellogin.h
    llpanelloginlistener.h
    llpanelmaininventory.h
    llpanelmarketplaceinbox.h
    llpanelmarketplaceinboxinventory.h
    llpanelmediasettingsgeneral.h
    llpanelmediasettingspermissions.h
    llpanelmediasettingssecurity.h
    llpanelnearbymedia.h
    llpanelobject.h
    llpanelobjectinventory.h
    llpanelonlinestatus.h
    llpaneloutfitedit.h
    llpaneloutfitsinventory.h
    llpanelpeople.h
    llpanelpeoplemenus.h
    llpanelpermissions.h
    llpanelplaceinfo.h
    llpanelplaceprofile.h
    llpanelplaces.h
    llpanelplacestab.h
    llpanelpresetscamerapulldown.h
    llpanelpresetspulldown.h
    llpanelprimmediacontrols.h
    llpanelprofile.h
    llpanelprofileclassifieds.h
    kvpanelprofilemetadata.h
    llpanelprofilepicks.h
    llpanelsnapshot.h
    llpanelteleporthistory.h
    llpaneltiptoast.h
    llpanelpulldown.h
    llpanelvoicedevicesettings.h
    llpaneltopinfobar.h
    llpanelvolume.h
    llpanelvolumepulldown.h
    llpanelwearing.h
    llparcelselection.h
    llparticipantlist.h
    llpatchvertexarray.h
    llpathfindingcharacter.h
    llpathfindingcharacterlist.h
    llpathfindinglinkset.h
    llpathfindinglinksetlist.h
    llpathfindingmanager.h
    llpathfindingnavmesh.h
    llpathfindingnavmeshstatus.h
    llpathfindingnavmeshzone.h
    llpathfindingobject.h
    llpathfindingobjectlist.h
    llpathfindingpathtool.h
    llpbrterrainfeatures.h
    llpersistentnotificationstorage.h
    llphysicsmotion.h
    llphysicsshapebuilderutil.h
    llpipelinelistener.h
    llplacesinventorybridge.h
    llplacesinventorypanel.h
    llplacesfolderview.h
    llpopupview.h
    llpostcard.h
    llpresetsmanager.h
    llpreview.h
    llpreviewanim.h
    llpreviewgesture.h
    llpreviewnotecard.h
    llpreviewscript.h
    llpreviewsound.h
    llpreviewtexture.h
    llproductinforequest.h
    llprogressview.h
    llrecentpeople.h
    llreflectionmap.h
    llreflectionmapmanager.h
    llheroprobemanager.h
    llregioninfomodel.h
    llregionposition.h
    llremoteparcelrequest.h
    llresourcedata.h
    llrootview.h
    llsaveoutfitcombobtn.h
    llscenemonitor.h
    llsceneview.h
    llscreenchannel.h
    llscripteditor.h
    llscriptfloater.h
    llscriptruntimeperms.h
    llscrollingpanelparam.h
    llscrollingpanelparambase.h
    llsculptidsize.h
    llsearchableui.h
    llsearchcombobox.h
    llsearchhistory.h
    llsecapi.h
    llsechandler_basic.h
    llselectmgr.h
    llsetkeybinddialog.h
    llsettingspicker.h
    llsettingsvo.h
    llsidepanelappearance.h
    llsidepanelinventory.h
    llsidepanelinventorysubpanel.h
    llsidepaneliteminfo.h
    llsidepaneltaskinfo.h
    llsidetraypanelcontainer.h
    llskinningutil.h
    llsky.h
    llslurl.h
    llsnapshotlivepreview.h
    llsnapshotmodel.h
    llspatialpartition.h
    llspeakers.h
    llspeakingindicatormanager.h
    llsplitbutton.h
    llsprite.h
    llstartup.h
    llstartuplistener.h
    llstatusbar.h
    llstylemap.h
    llsurface.h
    llsurfacepatch.h
    llsyntaxid.h
    llsyswellitem.h
    llsyswellwindow.h
    lltable.h
    llteleporthistory.h
    llteleporthistorystorage.h
    llterrainpaintmap.h
    lltexturecache.h
    lltexturectrl.h
    lltexturefetch.h
    lltextureinfo.h
    lltextureinfodetails.h
    lltexturestats.h
    lltextureview.h
    llthumbnailctrl.h
    lltinygltfhelper.h
    lltoast.h
    lltoastalertpanel.h
    lltoastgroupnotifypanel.h
    lltoastimpanel.h
    lltoastnotifypanel.h
    lltoastpanel.h
    lltoastscripttextbox.h
    lltoastscriptquestion.h
    lltool.h
    lltoolbarview.h
    lltoolbrush.h
    lltoolcomp.h
    lltooldraganddrop.h
    lltoolface.h
    lltoolfocus.h
    lltoolgrab.h
    lltoolgun.h
    lltoolindividual.h
    lltoolmgr.h
    lltoolmorph.h
    lltoolobjpicker.h
    lltoolpie.h
    lltoolpipette.h
    lltoolplacer.h
    lltoolselect.h
    lltoolselectland.h
    lltoolselectrect.h
    lltracker.h
    lltrackpicker.h
    lltransientdockablefloater.h
    lltransientfloatermgr.h
    lltranslate.h
    lluiconstants.h
    lluiavatar.h
    lluilistener.h
    lluploaddialog.h
    lluploadfloaterobservers.h
    llurl.h
    llurldispatcher.h
    llurldispatcherlistener.h
    llurlfloaterdispatchhandler.h
    llurlhistory.h
    llurllineeditorctrl.h
    llurlwhitelist.h
    llversioninfo.h
    llviewchildren.h
    llviewerassetstats.h
    llviewerassetstorage.h
    llviewerassettype.h
    llviewerassetupload.h
    llviewerattachmenu.h
    llvieweraudio.h
    llviewercamera.h
    llviewerchat.h
    llviewercontrol.h
    llviewercontrollistener.h
    llviewerdisplay.h
    llviewerdisplayname.h
    llviewerfloaterreg.h
    llviewerfoldertype.h
    llviewergenericmessage.h
    llviewergesture.h
    llviewerhelp.h
    llviewerhome.h
    llviewerinventory.h
    llviewerjoint.h
    llviewerjointattachment.h
    llviewerjointmesh.h
    llviewerjoystick.h
    llviewerinput.h
    llviewerlayer.h
    llviewermedia.h
    llviewermediafocus.h
    llviewermediaobserver.h
    llviewermenu.h
    llviewermenufile.h
    llviewermessage.h
    llviewernetwork.h
    llviewerobject.h
    llviewerobjectlist.h
    llvieweroctree.h
    llviewerparcelaskplay.h
    llviewerparcelmedia.h
    llviewerparcelmediaautoplay.h
    llviewerparcelmgr.h
    llviewerparceloverlay.h
    llviewerpartsim.h
    llviewerpartsource.h
    llviewerprecompiledheaders.h
    llviewerregion.h
    llviewershadermgr.h
    llviewerstats.h
    llviewerstatsrecorder.h
    llviewertexlayer.h
    llviewertexteditor.h
    llviewertexture.h
    llviewertextureanim.h
    llviewertexturelist.h
    llviewerthrottle.h
    llviewerwearable.h
    llviewerwindow.h
    llviewerwindowlistener.h
    llvlcomposition.h
    llvlmanager.h
    llvoavatar.h
    llvoavatarself.h
    llvocache.h
    llvograss.h
    llvoicechannel.h
    llvoiceclient.h
    llvoicevisualizer.h
    llvoicewebrtc.h
    llvoinventorylistener.h
    llvopartgroup.h
    llvosky.h
    llvosurfacepatch.h
    llvotree.h
    llvovolume.h
    llvowater.h
    llvowlsky.h
    llwatchdog.h
    llwearableitemslist.h
    llwearablelist.h
    llweb.h
    llwebprofile.h
    llwind.h
    llwindowlistener.h
    llwlhandlers.h
    llworld.h
    llworldmap.h
    llworldmapmessage.h
    llworldmipmap.h
    llworldmapview.h
    llxmlrpclistener.h
    llxmlrpctransaction.h
    noise.h
    pipeline.h
    roles_constants.h
    VertexCache.h
    VorbisFramework.h
    )

  list(APPEND viewer_SOURCE_FILES llperfstats.cpp)
  list(APPEND viewer_HEADER_FILES llperfstats.h)

source_group("CMake Rules" FILES ViewerInstall.cmake)

#build_data.json creation moved to viewer_manifest.py MAINT-6413
# the viewer_version.txt file created here is for passing to viewer_manifest and autobuild
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/viewer_version.txt"
           "${VIEWER_SHORT_VERSION}.${VIEWER_VERSION_REVISION}\n")

set_source_files_properties(
   llversioninfo.cpp 
   PROPERTIES
   COMPILE_DEFINITIONS "${VIEWER_CHANNEL_VERSION_DEFINES}" # see BuildVersion.cmake
   )




# Windows-specific source files
list(APPEND viewer_SOURCE_FILES
     llappviewerwin32.cpp
     llwindebug.cpp
     )

list(APPEND viewer_HEADER_FILES
     llappviewerwin32.h
     llwindebug.h
     )

    # Precompiled header configuration
    # All sources added to viewer_SOURCE_FILES at this point use it.
    set_source_files_properties(llviewerprecompiledheaders.cpp
      PROPERTIES
      COMPILE_FLAGS "/Ycllviewerprecompiledheaders.h /Zm150"
      )        
    foreach(src_file ${viewer_SOURCE_FILES})
      set_source_files_properties(
        ${src_file}
        PROPERTIES
        COMPILE_FLAGS "/Yullviewerprecompiledheaders.h /Zm150"
      )        
    endforeach(src_file ${viewer_SOURCE_FILES})
    list(APPEND viewer_SOURCE_FILES llviewerprecompiledheaders.cpp)

    # Add resource files to the project.
    # viewerRes.rc is the only buildable file, but
    # the rest are all dependencies of it.
    set(viewer_RESOURCE_FILES
        res/arrow.cur
        res/arrowcop.cur
        res/arrowcopmulti.cur
        res/arrowdrag.cur
        res/circleandline.cur
        res/icon1.ico
        res/llarrow.cur
        res/llarrowdrag.cur
        res/llarrowdragmulti.cur
        res/llarrowlocked.cur
        res/llgrablocked.cur
        res/llno.cur
        res/llnolocked.cur
        res/lltoolcamera.cur
        res/lltoolcreate.cur
        res/lltoolfocus.cur
        res/lltoolgrab.cur
        res/lltoolland.cur
        res/lltoolpan.cur
        res/lltoolpathfinding.cur
        res/lltoolpathfindingpathend.cur
        res/lltoolpathfindingpathendadd.cur
        res/lltoolpathfindingpathstart.cur
        res/lltoolpathfindingpathstartadd.cur
        res/lltoolpipette.cur
        res/lltoolrotate.cur
        res/lltoolscale.cur
        res/lltooltranslate.cur
        res/lltoolzoomin.cur
        res/lltoolzoomout.cur
        res/ll_icon.BMP
        res/kirstens-S24_icon.ico
        res/ll_icon_small.ico
        res/resource.h
        res/toolpickobject.cur
        res/toolpickobject2.cur
        res/toolpickobject3.cur
        res/toolpipette.cur
        res/toolbuy.cur
        res/toolopen.cur
        res/toolsit.cur
        )

    set_source_files_properties(${viewer_RESOURCE_FILES}
                                PROPERTIES HEADER_FILE_ONLY TRUE)

    configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/res/viewerRes.rc
                    ${CMAKE_CURRENT_BINARY_DIR}/viewerRes.rc
                    )
    set(viewer_RESOURCE_FILES
        ${CMAKE_CURRENT_BINARY_DIR}/viewerRes.rc
        ${viewer_RESOURCE_FILES}
        )

    set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/viewerRes.rc
      PROPERTIES COMPILE_FLAGS "-I${CMAKE_CURRENT_SOURCE_DIR}/res"
      )

    SOURCE_GROUP("Resource Files" FILES ${viewer_RESOURCE_FILES})

    list(APPEND viewer_SOURCE_FILES ${viewer_RESOURCE_FILES})

    set(viewer_LIBRARIES
        opengl32
        ${WINDOWS_LIBRARIES}
        comdlg32
        dxguid
        imm32
        kernel32
        odbc32
        odbccp32
        oleaut32
        shell32
        Vfw32
        wer
        winspool
        CfgMgr32        # <-- for OpenCL
        )

    find_library(INTEL_MEMOPS_LIBRARY
                 NAMES ll_intel_memops
                 PATHS
                 optimized ${ARCH_PREBUILT_DIRS_RELEASE}
                 debug ${ARCH_PREBUILT_DIRS_DEBUG}
                 )
    mark_as_advanced(INTEL_MEMOPS_LIBRARY)

    if(INTEL_MEMOPS_LIBRARY)
      list(APPEND viewer_LIBRARIES ${INTEL_MEMOPS_LIBRARY})
    endif()

# Add the xui files. This is handy for searching for xui elements
# from within the IDE.
set(viewer_XUI_FILES
    skins/default/colors.xml
    skins/default/textures/textures.xml
    )
file(GLOB DEFAULT_XUI_FILE_GLOB_LIST
     ${CMAKE_CURRENT_SOURCE_DIR}/skins/*/xui/en/*.xml)
list(APPEND viewer_XUI_FILES ${DEFAULT_XUI_FILE_GLOB_LIST})

file(GLOB DEFAULT_WIDGET_FILE_GLOB_LIST
     ${CMAKE_CURRENT_SOURCE_DIR}/skins/*/xui/en/widgets/*.xml)
list(APPEND viewer_XUI_FILES ${DEFAULT_WIDGET_FILE_GLOB_LIST})

list(SORT viewer_XUI_FILES)

source_group("XUI Files" FILES ${viewer_XUI_FILES})

set_source_files_properties(${viewer_XUI_FILES}
                            PROPERTIES HEADER_FILE_ONLY TRUE)

list(APPEND viewer_SOURCE_FILES ${viewer_XUI_FILES})

set(viewer_APPSETTINGS_FILES
    app_settings/anim.ini
    app_settings/autoreplace.xml
    app_settings/cmd_line.xml
    app_settings/commands.xml
    app_settings/emoji_groups.xml
    app_settings/foldertypes.xml
    app_settings/grass.xml
    app_settings/ignorable_dialogs.xml
    app_settings/key_bindings.xml
    app_settings/keywords_lsl_default.xml
    app_settings/logcontrol.xml
    app_settings/settings.xml
    app_settings/settings_files.xml
    app_settings/settings_per_account.xml
    app_settings/std_bump.ini
    app_settings/toolbars.xml
    app_settings/trees.xml
    app_settings/viewerart.xml
    ${CMAKE_SOURCE_DIR}/../etc/message.xml
    ${CMAKE_SOURCE_DIR}/../scripts/messages/message_template.msg
    featuretable.txt
    )

source_group("App Settings" FILES ${viewer_APPSETTINGS_FILES})

set_source_files_properties(${viewer_APPSETTINGS_FILES}
                            PROPERTIES HEADER_FILE_ONLY TRUE)

list(APPEND viewer_SOURCE_FILES ${viewer_APPSETTINGS_FILES})

set(viewer_CHARACTER_FILES
    character/attentions.xml
    character/attentionsN.xml
    character/avatar_lad.xml
    character/avatar_skeleton.xml
    character/genepool.xml
    )

source_group("Character File" FILES ${viewer_CHARACTER_FILES})

set_source_files_properties(${viewer_CHARACTER_FILES}
                            PROPERTIES HEADER_FILE_ONLY TRUE)

list(APPEND viewer_SOURCE_FILES ${viewer_CHARACTER_FILES})

# Windows installer files
file(GLOB viewer_INSTALLER_FILES installers/windows/*.nsi)

source_group("Installer Files" FILES ${viewer_INSTALLER_FILES})

set_source_files_properties(${viewer_INSTALLER_FILES}
                            PROPERTIES HEADER_FILE_ONLY TRUE)

list(APPEND viewer_SOURCE_FILES ${viewer_INSTALLER_FILES})

if(OPENAL)
  set(LLSTARTUP_COMPILE_FLAGS "${LLSTARTUP_COMPILE_FLAGS} -DLL_OPENAL")
endif()

if (FMODSTUDIO)
  set(LLSTARTUP_COMPILE_FLAGS "${LLSTARTUP_COMPILE_FLAGS} -DLL_FMODSTUDIO")
  set(FMODWRAPPER_LIBRARY ${FMODSTUDIO_LIBRARY})
endif (FMODSTUDIO)

set_source_files_properties(llstartup.cpp PROPERTIES COMPILE_FLAGS "${LLSTARTUP_COMPILE_FLAGS}")

list(APPEND viewer_SOURCE_FILES ${viewer_HEADER_FILES})

set_source_files_properties(${viewer_HEADER_FILES}
                            PROPERTIES HEADER_FILE_ONLY TRUE)

add_executable(${VIEWER_BINARY_NAME}
    WIN32
    ${viewer_SOURCE_FILES}
    )

# add package files
file(GLOB EVENT_HOST_SCRIPT_GLOB_LIST
     ${CMAKE_CURRENT_SOURCE_DIR}/../viewer_components/*.py)
list(APPEND EVENT_HOST_SCRIPTS ${EVENT_HOST_SCRIPT_GLOB_LIST})

set(PACKAGE ON CACHE BOOL
    "Add a package target that builds an installer package.")

if(WINDOWS)
  set_target_properties(${VIEWER_BINARY_NAME}
      PROPERTIES
      LINK_FLAGS "/debug /NODEFAULTLIB:LIBCMT /SUBSYSTEM:WINDOWS /LARGEADDRESSAWARE ws2_32.lib mswsock.lib advapi32.lib gdi32.lib shell32.lib crypt32.lib secur32.lib rpcrt4.lib winmm.lib dbghelp.lib netapi32.lib user32.lib ole32.lib wldap32.lib psapi.lib shlwapi.lib"
      LINK_FLAGS_DEBUG "/NODEFAULTLIB:\"LIBCMT;LIBCMTD;MSVCRT\" /INCREMENTAL:NO /LARGEADDRESSAWARE"
      LINK_FLAGS_RELEASE "/FORCE:MULTIPLE /MAP\"kirstens-s24.MAP\" /OPT:REF /LARGEADDRESSAWARE ws2_32.lib mswsock.lib advapi32.lib gdi32.lib shell32.lib crypt32.lib secur32.lib rpcrt4.lib winmm.lib dbghelp.lib netapi32.lib user32.lib ole32.lib wldap32.lib psapi.lib shlwapi.lib"
      )

  target_compile_options(${VIEWER_BINARY_NAME} PRIVATE "/Zc:wchar_t-")

  add_custom_command(
    TARGET ${VIEWER_BINARY_NAME} PRE_BUILD
    COMMAND ${CMAKE_COMMAND}
    ARGS
	  -E
	  copy_if_different
	  ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/messages/message_template.msg
	  ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/app_settings/message_template.msg
	COMMENT "Copying message_template.msg to the runtime folder."
  )

	# KV S24: PowerShell-only manifest copy (Python removed!)
	add_custom_command(
	  OUTPUT  ${CMAKE_CFG_INTDIR}/copy_touched.bat
	  COMMAND ${PWSH_EXECUTABLE} -ExecutionPolicy Bypass -NoProfile
		-File ${CMAKE_CURRENT_SOURCE_DIR}/Copy-ViewerManifest.ps1
		-BuildDir ${CMAKE_CURRENT_BINARY_DIR}
		-Configuration ${CMAKE_CFG_INTDIR}
		-SourceDir ${CMAKE_CURRENT_SOURCE_DIR}
		-Touch ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/copy_touched.bat
	  DEPENDS
		${CMAKE_CURRENT_SOURCE_DIR}/Copy-ViewerManifest.ps1
		${COPY_INPUT_DEPENDENCIES}
	  COMMENT "Copying essential viewer files (PowerShell)"
	  VERBATIM
	  )

	add_custom_target(copy_w_viewer_manifest ALL DEPENDS ${CMAKE_CFG_INTDIR}/copy_touched.bat)

	add_dependencies(${VIEWER_BINARY_NAME} llcommon copy_w_viewer_manifest)

	add_dependencies(${VIEWER_BINARY_NAME}
	  SLPlugin
	# S24 no crash logger
	)

    # sets the 'working directory' for visual studio.
    if(NOT UNATTENDED)
        set(WORKING_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        message(STATUS "Setting working directory to: ${WORKING_DIR}")
    endif()

    if(PACKAGE)
      # KV S24: PowerShell-only build system - we use winpack.ps1 instead
      # LL's event_host and llpackage targets not needed for KV Viewer

      # add_custom_command(
      #   OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/event_host.tar.bz2
      #   COMMAND ${PYTHON_EXECUTABLE}
      #   ARGS
      #     ${CMAKE_CURRENT_SOURCE_DIR}/event_host_manifest.py
      #     ${CMAKE_CURRENT_SOURCE_DIR}/..
      #     ${CMAKE_CURRENT_BINARY_DIR}
      #     ${CMAKE_CFG_INTDIR}
      #   DEPENDS
      #     lleventhost
      #     ${EVENT_HOST_SCRIPTS}
      #     ${CMAKE_CURRENT_SOURCE_DIR}/event_host_manifest.py
      #   )

      # add_custom_command(
      #   OUTPUT ${CMAKE_CFG_INTDIR}/touched.bat
      #   COMMAND ${PYTHON_EXECUTABLE}
      #   ARGS
      #     ${CMAKE_CURRENT_SOURCE_DIR}/viewer_manifest.py
      #     --arch=${ARCH}
      #     --artwork=${ARTWORK_DIR}
      #     --build=${CMAKE_CURRENT_BINARY_DIR}
      #     --buildtype=${CMAKE_BUILD_TYPE}
      #     "--channel=${VIEWER_CHANNEL}"
      #     --configuration=${CMAKE_CFG_INTDIR}
      #     --dest=${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}
      #     --grid=${GRID}
      #     --source=${CMAKE_CURRENT_SOURCE_DIR}
      #     --touch=${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/touched.bat
      #     --versionfile=${CMAKE_CURRENT_BINARY_DIR}/viewer_version.txt
      #   DEPENDS
      #       ${VIEWER_BINARY_NAME}
      #       ${CMAKE_CURRENT_SOURCE_DIR}/viewer_manifest.py
      #       ${CMAKE_CURRENT_BINARY_DIR}/viewer_version.txt
      #       ${COPY_INPUT_DEPENDENCIES}
      #   )

      # add_custom_target(llpackage ALL DEPENDS
      #   ${CMAKE_CFG_INTDIR}/touched.bat
      #   windows-setup-build-all
      #   )

      message(STATUS "KV S24: PowerShell-only build system. Use winpack.ps1 for packaging.")

  endif(PACKAGE)
endif(WINDOWS)

target_link_libraries(${VIEWER_BINARY_NAME}
    ${LEGACY_STDIO_LIBS}
    ${PNG_PRELOAD_ARCHIVES}
    ${ZLIB_PRELOAD_ARCHIVES}

    llaudio
    ${LLCHARACTER_LIBRARIES}
    ${LLIMAGE_LIBRARIES}
    ${LLINVENTORY_LIBRARIES}
    ${LLMESHOPTIMIZER_LIBRARIES}
    ${LLMESSAGE_LIBRARIES}
    ${LLPLUGIN_LIBRARIES}
    ${LLPRIMITIVE_LIBRARIES}
    ${LLRENDER_LIBRARIES}
    ${DXRENDER_LIBRARIES}
    ${FREETYPE_LIBRARIES}
    ${LLUI_LIBRARIES}
    ${LLFILESYSTEM_LIBRARIES}
    ${LLWINDOW_LIBRARIES}
    ${LLXML_LIBRARIES}
    ${LLMATH_LIBRARIES}
    ${LLCONVEXDECOMPOSITION_LIBRARIES}   # <-- added
    ${LLCOREHTTP_LIBRARIES}
    ${LLCOMMON_LIBRARIES}
    ${NDOF_LIBRARY}
    ${NVAPI_LIBRARY}
    ${HUNSPELL_LIBRARY}
    ${BOOST_PROGRAM_OPTIONS_LIBRARY}
    ${BOOST_REGEX_LIBRARY}
    ${BOOST_FIBER_LIBRARY}
    ${BOOST_CONTEXT_LIBRARY}
    ll::opencl
    ${OPENGL_LIBRARIES}
    ${FMODWRAPPER_LIBRARY} # must come after LLAudio
    ${GLOD_LIBRARIES}
    ${JSONCPP_LIBRARIES}
    ${SDL_LIBRARY}
    ${SMARTHEAP_LIBRARY}
    ${UI_LIBRARIES}
    ${EXPAT_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    ${CRYPTO_LIBRARIES}
    ${LLLOGIN_LIBRARIES}
    ${LLPHYSICS_LIBRARIES}
    ${LLPHYSICSEXTENSIONS_LIBRARIES}
    ${LLAPPEARANCE_LIBRARIES}
    llwebrtc

    # --- System libraries LAST to resolve all static lib deps above ---
    ${viewer_LIBRARIES}
    ${WINDOWS_LIBRARIES}
    )

set(ARTWORK_DIR ${CMAKE_CURRENT_SOURCE_DIR} CACHE PATH
    "Path to artwork files.")

if (INSTALL)
  include(${CMAKE_CURRENT_SOURCE_DIR}/ViewerInstall.cmake)
endif (INSTALL)

if (PACKAGE)
  set(SYMBOL_SEARCH_DIRS "")
 
    list(APPEND SYMBOL_SEARCH_DIRS "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}")
    # slplugin.exe failing symbols dump - need to debug, might have to do with updated version of google breakpad
    # set(VIEWER_EXE_GLOBS "${VIEWER_BINARY_NAME}${CMAKE_EXECUTABLE_SUFFIX} slplugin.exe")
    set(VIEWER_EXE_GLOBS "${VIEWER_BINARY_NAME}${CMAKE_EXECUTABLE_SUFFIX}")
    set(VIEWER_LIB_GLOB "*${CMAKE_SHARED_MODULE_SUFFIX}")
    set(VIEWER_COPY_MANIFEST copy_w_viewer_manifest)
         
endif (PACKAGE)


# KV S24: PowerShell-only template verification (Python removed!)
check_message_template_modern(${VIEWER_BINARY_NAME})

