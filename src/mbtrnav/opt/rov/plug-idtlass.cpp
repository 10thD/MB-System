#include "trnx_plugin.hpp"

// g++ -c -g -O2 -std=c++11 -I. -I../../utils -I../../qnx-utils -I../../terrain-nav -I../../newmat -I../../trnw -I/usr/local/include -I/opt/local/include -fPIC plug-idt.cpp

// g++ -shared -o libdvl.so plug-idt.o -L../../bin -L/usr/local/lib -L/opt/local/lib -L. -lnewmat -lgeolib -ltrnw -lqnx -lmb1 -ltrn -ludpms -ltrncli -lnetcdf -llcm -ldvl

// Transform IDT beams given nav/attitude
// mounted LASS tilting sled
// expects:
// b[0]   : vehicle bath (deltaT)
// a[0]   : vehicle attitude
// a[1]   : sled attitude
// n[0]   : vehicle navigation
// n[1]   : sled navigation
// geo[0] : mbgeo (multibeam geometry)
// geo[1] : txgeo (sled INS geometry)
// geo[2] : txgeo (veh nav geometry)

int transform_idtlass(trn::bath_info **bi, trn::att_info **ai, beam_geometry **bgeo, mb1_t *r_snd)
{
    int retval = -1;

    // validate inputs
    if(NULL == bgeo || bgeo[0] == nullptr || bgeo[1] == nullptr || bgeo[2] == nullptr){
        fprintf(stderr, "%s - geometry error : NULL input bgeo[%p] {%p, %p, %p} \n", __func__, bgeo, (bgeo?bgeo[0]:nullptr), (bgeo?bgeo[1]:nullptr), (bgeo?bgeo[2]:nullptr));
        return retval;
    }

    // IDT geometry
    mbgeo *mb_geo[1] = {static_cast<mbgeo *>(bgeo[0])};

    // 0: sled INS geometry
    // 1: veh nav geometry
    txgeo *tx_geo[2] = {static_cast<txgeo *>(bgeo[1]), static_cast<txgeo *>(bgeo[2])};

    if(mb_geo[0] && mb_geo[0]->beam_count <= 0){
        fprintf(stderr, "%s - geometry warning : geo[0] beams <= 0 {%u}\n", __func__, mb_geo[0]->beam_count);
    }

    if(NULL == r_snd || NULL == ai|| NULL == bi){
        fprintf(stderr, "%s - ERR invalid argument bi[%p] ai[%p] snd[%p]\n", __func__, bi, ai, r_snd);
        return retval;
    }

    if(NULL == ai[0] || NULL == ai[1] || NULL == bi[0]){
        fprintf(stderr, "%s - ERR invalid info ai[0][%p] ai[1][%p] bi[0][%p] \n", __func__, ai[0], ai[1], bi[0]);
        return retval;
    }

    // vehicle attitude (relative to NED, radians)
    // r/p/y  (phi/theta/psi)
    // MB1 assumes vehicle frame, not world frame (i.e. exclude heading)
    double VATT[3] = {ai[0]->roll(), ai[0]->pitch(), 0.};

    // sensor mounting angles (relative to vehicle, radians)
    // 3-2-1 euler angles, r/p/y  (phi/theta/psi)
    // wrt sensor mounted across track, b[0] port, downward facing
    double SROT[3] = { DTR(mb_geo[0]->svr_deg[0]), DTR(mb_geo[0]->svr_deg[1]), DTR(mb_geo[0]->svr_deg[2])};

    // sensor mounting translation offsets (relative to vehicle CRP, meters)
    // +x: fwd +y: stbd, +z:down
    // or should it be relative to location sensor?
    double STRN[3] = {mb_geo[0]->svt_m[0], mb_geo[0]->svt_m[1], mb_geo[0]->svt_m[2]};

    double XTRN[3] = {mb_geo[0]->rot_radius_m, 0., 0.};
    double XR = 0;//ai[1]->pitch() - ai[0]->pitch();
    double XROT[3] = {0., XR, 0.};

    Matrix beams_SF = trnx_utils::mb_sframe_components(bi[0], mb_geo[0]);

    TRN_NDPRINT(TRNDL_PLUGIDTLASS, "%s: --- \n",__func__);
    TRN_NDPRINT(TRNDL_PLUGIDTLASS, "mb_geo:\n%s\n",mb_geo[0]->tostring().c_str());
    TRN_NDPRINT(TRNDL_PLUGIDTLASS, "tx_geo.0:\n%s\n",tx_geo[0]->tostring().c_str());
    TRN_NDPRINT(TRNDL_PLUGIDTLASS, "tx_geo.1:\n%s\n",tx_geo[1]->tostring().c_str());


    TRN_NDPRINT(TRNDL_PLUGIDTLASS, "VATT[%.3lf, %.3lf, %.3lf]\n", VATT[0], VATT[1], VATT[2]);
    TRN_NDPRINT(TRNDL_PLUGIDTLASS, "SROT[%.3lf, %.3lf, %.3lf]\n", SROT[0], SROT[1], SROT[2]);
    TRN_NDPRINT(TRNDL_PLUGIDTLASS, "STRN[%.3lf, %.3lf, %.3lf]\n", STRN[0], STRN[1], STRN[2]);

    const char *pinv = (ai[0]->flags().is_set(trn::AF_INVERT_PITCH)? "(p-)" :"(p+)");

    TRN_NDPRINT(TRNDL_PLUGIDTLASS, "VATT (deg) [%.2lf, %.2lf, %.2lf (%.2lf)] %s\n",
                Math::radToDeg(VATT[0]), Math::radToDeg(VATT[1]), Math::radToDeg(VATT[2]), Math::radToDeg(ai[0]->heading()), pinv);
    TRN_NDPRINT(TRNDL_PLUGIDTLASS, "XTRN[%.3lf, %.3lf, %.3lf]\n", XTRN[0], XTRN[1], XTRN[2]);
    TRN_NDPRINT(TRNDL_PLUGIDTLASS, "XROT[%.3lf, %.3lf, %.3lf]\n", XROT[0], XROT[1], XROT[2]);
    TRN_NDPRINT(TRNDL_PLUGIDTLASS, "pitch (deg) veh[%.3lf] ois[%.3lf] angle[%.3lf]\n", Math::radToDeg(ai[0]->pitch()), Math::radToDeg(ai[1]->pitch()), Math::radToDeg(XR));
    TRN_NDPRINT(5,"\n");
#if 0
    // generate coordinate tranformation matrices
    // translate arm rotation point to sled origin
    Matrix mat_XTRN = trnx_utils::affineTranslation(XTRN);
    // sled arm rotation
    Matrix mat_XROT = trnx_utils::affine321Rotation(XROT);
    // mounting rotation matrix
    Matrix mat_SROT = trnx_utils::affine321Rotation(SROT);
    // mounting translation matrix
    Matrix mat_STRN = trnx_utils::affineTranslation(STRN);
    // vehicle attitude (pitch, roll, heading)
    Matrix mat_VATT = trnx_utils::affine321Rotation(VATT);

    // combine to get composite tranformation
    // order is significant:
    // mounting rotations, translate
    Matrix S0 = mat_XTRN * mat_SROT;
    // arm rotation
    Matrix S1 = mat_XROT * S0;
    // translate to position on arm
    Matrix S2 = mat_STRN * S1;
    // appy vehicle attitude
    Matrix Q = mat_VATT * S2;

    // apply coordinate transforms
    Matrix beams_VF = Q * beams_SF;
#endif

    // 2023/10/10 : first order
    // - uncompensated lat/lon from sled kearfott
    // - use vehicle depth ()

    // mounting rotation matrix
    Matrix mat_SROT = trnx_utils::affine321Rotation(SROT);
    // mounting translation matrix
    Matrix mat_STRN = trnx_utils::affineTranslation(STRN);
    // vehicle attitude (pitch, roll, heading)
    Matrix mat_VATT = trnx_utils::affine321Rotation(VATT);

    // combine to get composite tranformation
    // order is significant:

    // apply IDT mounting translation, rotation
    Matrix S0 = mat_SROT * mat_STRN;
    // apply vehicle attitude
    Matrix S1 = mat_VATT * S0;

    // apply coordinate transforms
    Matrix beams_VF = S1 * beams_SF;

    // snd is intialized with vehicle nav
    // if initialized w/ sled INS
    // adjust depth for mounting offset difference (Z+ down)
    // Znav - Zmb
    r_snd->depth += (tx_geo[0]->tran_m[3] - tx_geo[1]->tran_m[3]);

    // fill in the MB1 record using transformed beams
    std::list<trn::beam_tup> beams = bi[0]->beams_raw();
    std::list<trn::beam_tup>::iterator it;

    int idx[2] = {0, 1};
    for(it=beams.begin(); it!=beams.end(); it++, idx[0]++, idx[1]++)
    {
        // write beam data to MB1 sounding
        trn::beam_tup bt = static_cast<trn::beam_tup> (*it);

        // beam number (0-indexed)
        int b = std::get<0>(bt);
        double range = std::get<1>(bt);
        // beam components WF x,y,z
        // matrix row/col (1 indexed)
        r_snd->beams[idx[0]].beam_num = b;
        r_snd->beams[idx[0]].rhox = range * beams_VF(1, idx[1]);
        r_snd->beams[idx[0]].rhoy = range * beams_VF(2, idx[1]);
        r_snd->beams[idx[0]].rhoz = range * beams_VF(3, idx[1]);

        if(trn_debug::get()->debug() >= TRNDL_PLUGIDTLASS){

            // calculated beam range (should match measured range)
            double rho[3] = {r_snd->beams[idx[0]].rhox, r_snd->beams[idx[0]].rhoy, r_snd->beams[idx[0]].rhoz};

            double rhoNorm = trnx_utils::vnorm( rho );

            // calculate component angles wrt vehicle axes
            double axr = (range==0. ? 0. :acos(r_snd->beams[idx[0]].rhox/range));
            double ayr = (range==0. ? 0. :acos(r_snd->beams[idx[0]].rhoy/range));
            double azr = (range==0. ? 0. :acos(r_snd->beams[idx[0]].rhoz/range));

            TRN_NDPRINT(TRNDL_PLUGIDTLASS_H, "%s: b[%3d] r[%7.2lf] R[%7.2lf]     rhox[%7.2lf] rhoy[%7.2lf] rhoz[%7.2lf]     ax[%6.2lf] ay[%6.2lf] az[%6.2lf]\n",
                        __func__, b, range, rhoNorm,
                        r_snd->beams[idx[0]].rhox,
                        r_snd->beams[idx[0]].rhoy,
                        r_snd->beams[idx[0]].rhoz,
                        Math::radToDeg(axr),
                        Math::radToDeg(ayr),
                        Math::radToDeg(azr)
                        );
        }
    }
    TRN_NDPRINT(TRNDL_PLUGIDTLASS, "%s: --- \n\n",__func__);

    retval = 0;
    return retval;
}

// OI Toolsled
// vehicle: octans, IDT (Imagenex DeltaT)
// sled: kearfott
// expects:
// b[0]   : vehicle bath (deltaT)
// a[0]   : vehicle attitude
// a[1]   : sled attitude
// n[0]   : vehicle navigation
// n[1]   : sled navigation
// geo[0] : mbgeo
// geo[1] : oigeo
int cb_proto_idtlass(void *pargs)
{
    int retval=-1;

    TRN_NDPRINT(TRNDL_PLUGIDTLASS_H, "%s:%d >>> Callback triggered <<<\n", __func__, __LINE__);

    trn::trnxpp::callback_res_t *cb_res = static_cast<trn::trnxpp::callback_res_t *>(pargs);
    trn::trnxpp *xpp = cb_res->xpp;
    trnxpp_cfg *cfg = cb_res->cfg;

    cfg->stats().trn_cb_n++;

    // iterate over contexts
    std::vector<trn::trnxpp_ctx *>::iterator it;
    for(it = xpp->ctx_list_begin(); it != xpp->ctx_list_end(); it++)
    {
        trn::trnxpp_ctx *ctx = (*it);
        // if context defined for this callback
        if(ctx == nullptr || !ctx->has_callback("cb_proto_idtlass"))
        {
            // skip invalid context
            continue;
        }

        TRN_NDPRINT(TRNDL_PLUGIDTLASS, "%s:%d processing ctx[%s]\n", __func__, __LINE__, ctx->ctx_key().c_str());

        int err_count = 0;

        std::string *bkey[1] = {ctx->bath_input_chan(0)};
        std::string *nkey[2] = {ctx->nav_input_chan(0),ctx->nav_input_chan(1)};
        std::string *akey[2] = {ctx->att_input_chan(0), ctx->att_input_chan(1)};
        std::string *vkey = ctx->vel_input_chan(0);

        // vi is optional
        if(bkey[0] == nullptr || nkey[0] == nullptr || nkey[1] == nullptr || akey[0] == nullptr || akey[1] == nullptr)
        {
            ostringstream ss;
            ss << (bkey[0]==nullptr ? " bkey[0]" : "");
            ss << (akey[0]==nullptr ? " akey[0]" : "");
            ss << (akey[1]==nullptr ? " akey[1]" : "");
            ss << (nkey[0]==nullptr ? " nkey[0]" : "");
            ss << (nkey[1]==nullptr ? " nkey[1]" : "");
            ss << (vkey == nullptr ? " vkey" : "");
            TRN_NDPRINT(TRNDL_PLUGIDTLASS, "%s:%d ERR - NULL input key: %s\n", __func__, __LINE__, ss.str().c_str());
                err_count++;
                continue;
        }

        trn::bath_info *bi[2] = {xpp->get_bath_info(*bkey[0]), nullptr};
        trn::nav_info *ni[2] = {xpp->get_nav_info(*nkey[0]), xpp->get_nav_info(*nkey[1])};
        trn::att_info *ai[2] = {xpp->get_att_info(*akey[0]), xpp->get_att_info(*akey[1])};
        trn::vel_info *vi = (vkey == nullptr ? nullptr : xpp->get_vel_info(*vkey));

        // vi optional
        if(bi[0] == nullptr || ni[0] == nullptr || ni[0] == nullptr || ai[1] == nullptr || ai[1] == nullptr)
        {
            ostringstream ss;
            ss << (bi[0] == nullptr ? " bi[0]" : "");
            ss << (ai[0] == nullptr ? " ai[0]" : "");
            ss << (ai[1] == nullptr ? " ai[1]" : "");
            ss << (ni[0] == nullptr ? " ni[0]" : "");
            ss << (ni[1] == nullptr ? " ni[1]" : "");
            ss << (vi == nullptr ? " vi" : "");
            TRN_NDPRINT(TRNDL_PLUGIDTLASS, "%s:%d WARN - NULL info instance: %s\n", __func__, __LINE__, ss.str().c_str());
                err_count++;
                continue;
        }

        if(bkey[0] != nullptr && bi[0] != nullptr)
            TRN_NDPRINT(TRNDL_PLUGIDTLASS_H, "BATHINST.%s : %s\n",bkey[0]->c_str(), bi[0]->bathstr());

        size_t n_beams = bi[0]->beam_count();

        if (n_beams > 0) {

            // generate MB1 sounding (raw beams)
            mb1_t *snd = trnx_utils::lcm_to_mb1(bi[0], ni[1], ai[0]);

//            fprintf(stderr,"%s - >>>>>>> new MB1 from lcm:\n",__func__);
//            mb1_show(snd, true, 5);

            std::list<trn::beam_tup> beams = bi[0]->beams_raw();
            std::list<trn::beam_tup>::iterator it;

            // if streams_ok, bs/bp pointers have been validated
            trn::bath_input *bp[1] = {xpp->get_bath_input(*bkey[0])};
            int trn_type[3] = {-1, trn::BT_NONE, trn::BT_NONE};

            if(nullptr != bp[0]) {
                trn_type[0] = bp[0]->bath_input_type();

                if(trn_type[0] == trn::BT_DELTAT)
                {
                    beam_geometry *bgeo[3] = {nullptr, nullptr};

                    // get geometry for IDT, sled INS, veh nav
                    bgeo[0] = xpp->lookup_geo(*bkey[0], trn_type[0]);
                    bgeo[1] = xpp->lookup_geo(*nkey[1], trn_type[1]);
                    bgeo[2] = xpp->lookup_geo(*nkey[0], trn_type[2]);

                    // compute MB1 beam components in vehicle frame
                    if (transform_idtlass(bi, ai, bgeo, snd) != 0) {
                        TRN_NDPRINT(TRNDL_PLUGIDTLASS_H, "%s:%d ERR - transform_idtlass failed\n", __func__, __LINE__);
                        err_count++;
                        continue;
                    }

                } else {
                    fprintf(stderr,"%s:%d ERR - unsupported input_type[%d] beam transformation invalid\n", __func__, __LINE__, trn_type[0]);
                }
            } else {
                fprintf(stderr,"%s:%d ERR - NULL bath input; skipping transforms\n", __func__, __LINE__);
            }

            mb1_set_checksum(snd);

            // check modulus
            if(ctx->decmod() <= 0 || (ctx->cbcount() % ctx->decmod()) == 0){

                if(cfg->debug() >= TRNDL_PLUGIDTLASS ){
                    fprintf(stderr,"%s - >>>>>>> Publishing MB1:\n",__func__);
                    mb1_show(snd, (cfg->debug()>=TRNDL_PLUGIDTLASS ? true: false), 5);
                }

                // publish MB1 to mbtrnpp
                ctx->pub_mb1(snd, xpp->pub_list(), cfg);

                if(ctx->trncli_count() > 0){

                    // publish poseT/measT to trn-server

                    poseT *pt = trnx_utils::mb1_to_pose(snd, ai[0], (long)ctx->utm_zone());
                    measT *mt = trnx_utils::mb1_to_meas(snd, ai[0], trn_type[0], (long)ctx->utm_zone());

                    if(cfg->debug() >= TRNDL_PLUGIDTLASS ){
                        fprintf(stderr,"%s - >>>>>>> Publishing POSE:\n",__func__);
                        trnx_utils::pose_show(*pt);
                        fprintf(stderr,"%s - >>>>>>> Publishing MEAS:\n",__func__);
                        trnx_utils::meas_show(*mt);
                    }


                    if(pt != nullptr && mt != nullptr){

                        double nav_time = ni[0]->time_usec()/1e6;

                        // publish update TRN, publish estimate to TRN, LCM
                        ctx->pub_trn(nav_time, pt, mt, trn_type[0], xpp->pub_list(), cfg);
                    }

                    if(pt != nullptr)
                        delete pt;
                    if(mt != nullptr)
                        delete mt;
                }

            } else {
                TRN_NDPRINT(TRNDL_PLUGIDTLASS, "%s:%d WARN - not ready count/mod[%d/%d]\n", __func__, __LINE__,ctx->cbcount(), ctx->decmod());
            }
            ctx->inc_cbcount();


            // write CSV
            if(ctx->write_mb1_csv(snd, bi[0], ai[0], vi) > 0){
                cfg->stats().mb_csv_n++;
            }

            ctx->write_mb1_bin(snd);

            retval=0;

            // release sounding memory
            mb1_destroy(&snd);
        }

        if(bi[0] != nullptr)
            delete bi[0];
        if(ai[0] != nullptr)
            delete ai[0];
        if(ai[1] != nullptr)
            delete ai[1];
        if(ni[0] != nullptr)
            delete ni[0];
        if(ni[1] != nullptr)
            delete ni[1];
        if(vi != nullptr)
            delete vi;
    }

    return retval;
}
