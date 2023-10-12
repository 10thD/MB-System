#include "trnx_plugin.hpp"

// g++ -c -g -O2 -std=c++11 -I. -I../../utils -I../../qnx-utils -I../../terrain-nav -I../../newmat -I../../trnw -I/usr/local/include -I/opt/local/include -fPIC plug-oisled2.cpp

// g++ -shared -o libdvl.so plug-oisled2.o -L../../bin -L/usr/local/lib -L/opt/local/lib -L. -lnewmat -lgeolib -ltrnw -lqnx -lmb1 -ltrn -ludpms -ltrncli -lnetcdf -llcm

// process MB1 sounding from MB1 LCM stream
// applies transform specified in geo
// expects:
// b[0]   : mb1
// a[0]   : vehicle attitude
// a[1]   : sled attitude
// geo[0] : mbgeo (transforms)
// with xmap : "atx", "aty", "atz"
// snd - sounding (w. navigation in vehicle frame)
void transform_xmb1(trn::mb1_info **bi, trn::att_info **ai, mbgeo **geo, mb1_t *r_snd)
{
    // validate inputs
    if(NULL == geo || geo[0] == nullptr){
        fprintf(stderr, "%s - geometry error : NULL input geo[%p] {%p} \n", __func__, geo, (geo?geo[0]:nullptr));
        return;
    }
    if(geo[0] && geo[0]->beam_count <= 0){
        fprintf(stderr, "%s - geometry warning : geo[0] beams <= 0 {%u}\n", __func__, geo[0]->beam_count);
    }
    if(NULL == r_snd || NULL == ai|| NULL == bi){
        fprintf(stderr, "%s - ERR invalid argument bi[%p] ai[%p] snd[%p]\n", __func__, bi, ai, r_snd);
        return;
    }

    if(NULL == ai[0] || NULL == ai[1] || NULL == bi[0]){
        fprintf(stderr, "%s - ERR invalid info ai[0][%p] ai[1][%p] bi[0][%p] \n", __func__, ai[0], ai[1], bi[0]);
        return;
    }

    // vehicle attitude (relative to NED)
    // r/p/y (phi/theta/psi)
    // MB1 assumes vehicle frame, not world frame (i.e. exclude heading)
    double VATT[3] = {ai[1]->roll(), ai[1]->pitch(), 0.};

    // sensor mounting angles (relative to vehicle, radians)
    // 3-2-1 euler angles, r/p/y  (phi/theta/psi)
    // wrt sensor mounted across track, b[0] port, downward facing
    double SROT[3] = { DTR(geo[0]->svr_deg[0]), DTR(geo[0]->svr_deg[1]), DTR(geo[1]->svr_deg[2])};

    // sensor mounting translation offsets (relative to vehicle CRP, meters)
    // +x: fwd +y: stbd, +z:down (aka FSK, fwd,stbd,keel)
    // TODO T is for transform use rSV
    double STRN[3] = {geo[0]->svt_m[0], geo[0]->svt_m[1], geo[0]->svt_m[2]};
    double ax = geo[0]->xmap["atx"];
    double ay = geo[0]->xmap["aty"];
    double az = geo[0]->xmap["atz"];
    double XTRN[3] = {ax, ay, az};
    double XR = ai[1]->pitch() - ai[0]->pitch();
    double XROT[3] = {0., XR, 0.};

    // beam components in reference sensor frame (mounted center, across track)
    Matrix beams_SF = trnx_utils::mb_sframe_components(bi[0], geo[0]);
    TRN_NDPRINT(TRNDL_PLUGXMB1, "%s: --- \n",__func__);
    TRN_NDPRINT(TRNDL_PLUGXMB1, "%s: geo[0]:\n%s\n",__func__,geo[0]->tostring().c_str());

    TRN_NDPRINT(TRNDL_PLUGXMB1, "VATT[%.3lf, %.3lf, %.3lf]\n", VATT[0], VATT[1], VATT[2]);
    TRN_NDPRINT(TRNDL_PLUGXMB1, "SROT[%.3lf, %.3lf, %.3lf]\n", SROT[0], SROT[1], SROT[2]);
    TRN_NDPRINT(TRNDL_PLUGXMB1, "STRN[%.3lf, %.3lf, %.3lf]\n", STRN[0], STRN[1], STRN[2]);

    const char *pinv = (ai[0]->flags().is_set(trn::AF_INVERT_PITCH)? "(p-)" :"(p+)");

    TRN_NDPRINT(TRNDL_PLUGXMB1, "VATT (deg) [%.2lf, %.2lf, %.2lf (%.2lf)] %s\n",
                Math::radToDeg(VATT[0]), Math::radToDeg(VATT[1]), Math::radToDeg(VATT[2]), Math::radToDeg(ai[0]->heading()), pinv);
    TRN_NDPRINT(TRNDL_PLUGXMB1, "XTRN[%.3lf, %.3lf, %.3lf]\n", XTRN[0], XTRN[1], XTRN[2]);
    TRN_NDPRINT(TRNDL_PLUGXMB1, "XROT[%.3lf, %.3lf, %.3lf]\n", XROT[0], XROT[1], XROT[2]);
    TRN_NDPRINT(TRNDL_PLUGXMB1, "pitch (deg) veh[%.3lf] ois[%.3lf] angle[%.3lf]\n", Math::radToDeg(ai[0]->pitch()), Math::radToDeg(ai[1]->pitch()), Math::radToDeg(XR));
    TRN_NDPRINT(TRNDL_PLUGXMB1, "\n");

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

    std::list<trn::mb1_beam_tup> beams = bi[0]->beams_raw();
    std::list<trn::mb1_beam_tup>::iterator it;

    // fill in the MB1 record using transformed beams
    // zero- and one-based indexs
    int idx[2] = {0, 1};
    for(it=beams.begin(); it!=beams.end(); it++, idx[0]++, idx[1]++)
    {
        // write beam data to MB1 sounding
        trn::mb1_beam_tup bt = static_cast<trn::mb1_beam_tup> (*it);

        // beam number (0-indexed)
        int b = std::get<0>(bt);
        double bx = std::get<1>(bt);
        double by = std::get<2>(bt);
        double bz = std::get<3>(bt);
        double range = sqrt(bx*bx + by*by + bz*bz);

        // beam components WF x,y,z
        // matrix row/col (1 indexed)
        r_snd->beams[idx[0]].beam_num = b;
        r_snd->beams[idx[0]].rhox = range * beams_VF(1, idx[1]);
        r_snd->beams[idx[0]].rhoy = range * beams_VF(2, idx[1]);
        r_snd->beams[idx[0]].rhoz = range * beams_VF(3, idx[1]);

        if(trn_debug::get()->debug() >= 5){

            // calculated beam range (should match measured range)
            double rho[3] = {r_snd->beams[idx[0]].rhox, r_snd->beams[idx[0]].rhoy, r_snd->beams[idx[0]].rhoz};

            double rhoNorm = trnx_utils::vnorm( rho );

            // calculate component angles wrt vehicle axes
            double axr = (range==0. ? 0. :acos(r_snd->beams[idx[0]].rhox/range));
            double ayr = (range==0. ? 0. :acos(r_snd->beams[idx[0]].rhoy/range));
            double azr = (range==0. ? 0. :acos(r_snd->beams[idx[0]].rhoz/range));

            TRN_NDPRINT(TRNDL_PLUGXMB1_H, "%s: b[%3d] r[%7.2lf] R[%7.2lf]     rhox[%7.2lf] rhoy[%7.2lf] rhoz[%7.2lf]     ax[%6.2lf] ay[%6.2lf] az[%6.2lf]\n",
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
    TRN_NDPRINT(TRNDL_PLUGXMB1, "%s: --- \n\n",__func__);

    return;
}

// input: MB1 (trn_mb1_t LCM; nav, heading, bathymetry)
// publish to: mbtrnpp , TRN server
// expects:
// b[0]   : mb1
// a[0]   : vehicle attitude
// a[1]   : sled attitude
// geo[0] : mbgeo (transforms)
int cb_proto_xmb1(void *pargs)
{
    int retval=-1;

    TRN_NDPRINT(3, "%s:%d >>> Callback triggered <<<\n", __func__, __LINE__);

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
        if(ctx == nullptr || !ctx->has_callback("cb_proto_xmb1"))
        {
            TRN_TRACE();
            // skip invalid context
            continue;
        }

        TRN_NDPRINT(5, "%s:%d processing ctx[%s]\n", __func__, __LINE__, ctx->ctx_key().c_str());

        int err_count = 0;

        std::string *bkey[1] = {ctx->mb1_input_chan(0)};
        std::string *nkey = ctx->nav_input_chan(0);
        std::string *akey[2] = {ctx->att_input_chan(0), ctx->att_input_chan(1)};
        std::string *vkey = ctx->vel_input_chan(0);

        // vi is optional
        if(bkey[0] == nullptr || nkey == nullptr || akey[0] == nullptr || akey[1] == nullptr)
        {
            ostringstream ss;
            ss << (bkey[0] == nullptr ? " bkey[0]" : "");
            ss << (akey[0] == nullptr ? " akey[0]" : "");
            ss << (akey[1] == nullptr ? " akey[1]" : "");
            ss << (nkey == nullptr ? " nkey" : "");
            TRN_NDPRINT(5, "%s:%d WARN - NULL input key: %s\n", __func__, __LINE__, ss.str().c_str());
            err_count++;
            continue;
        }

        trn::mb1_info *bi[1] = {xpp->get_mb1_info(*bkey[0])};
        trn::nav_info *ni = xpp->get_nav_info(*nkey);
        trn::att_info *ai[2] = {xpp->get_att_info(*akey[0]), xpp->get_att_info(*akey[1])};
        trn::vel_info *vi = (vkey == nullptr ? nullptr : xpp->get_vel_info(*vkey));

        // vi optional
        if(bi[0] == nullptr || ni == nullptr || ai[1] == nullptr || ai[1] == nullptr || vi == nullptr)
        {
            ostringstream ss;
            ss << (bi[0] == nullptr ? " bi[0]" : "");
            ss << (ai[0] == nullptr ? " ai[0]" : "");
            ss << (ai[1] == nullptr ? " ai[1]" : "");
            ss << (ni == nullptr ? " ni" : "");
            ss << (vi == nullptr ? " vi" : "");
            TRN_NDPRINT(5, "%s:%d WARN - NULL info instance: %s\n", __func__, __LINE__, ss.str().c_str());
            err_count++;
        }

        TRN_TRACE();

        if(bkey[0] != nullptr && bi[0] != nullptr)
            TRN_NDPRINT(6, "MB1INST.%s : %s\n",bkey[0]->c_str(), bi[0]->mb1str());

        // sled DVL beam count
        size_t n_beams = bi[0]->beam_count();

        if(n_beams > 0){
            TRN_TRACE();

            // use lcm-mb1 bathy, vehicle attitude
            mb1_t *snd = trnx_utils::lcm_to_mb1(bi[0], ni, ai[0]);

            std::list<trn::mb1_beam_tup> beams = bi[0]->beams_raw();
            std::list<trn::mb1_beam_tup>::iterator it;

            // if streams_ok, bs/bp pointers have been validated
            trn::mb1_input *bp[1] = {xpp->get_mb1_input(*bkey[0])};
            int trn_type[1] = {-1};

            if(bp[0] != nullptr){
                trn_type[0] = bp[0]->mb1_input_type();
            }

            if(nullptr != bp[0]) {

                mbgeo *geo[1] = {nullptr};
                beam_geometry *bgeo[1] = {nullptr};

                bgeo[0] = xpp->lookup_geo(*bkey[0], trn_type[0]);
                geo[0] = static_cast<mbgeo *>(bgeo[0]);

                // tranform MB1 beams
                transform_xmb1(bi, ai, geo, snd);
            } else {
                fprintf(stderr,"%s:%d ERR - NULL bath input; skipping transforms\n", __func__, __LINE__);
            }

            mb1_set_checksum(snd);

            // check modulus
            if(ctx->decmod() <= 0 || (ctx->cbcount() % ctx->decmod()) == 0){

                if(cfg->debug() >=4 ){
                    mb1_show(snd, (cfg->debug()>=5 ? true: false), 5);
                }

                // publish MB1 to mbtrnpp
                ctx->pub_mb1(snd, xpp->pub_list(), cfg);


                if(ctx->trncli_count() > 0){

                    // publish poseT/measT to trn-server
                    poseT *pt = trnx_utils::mb1_to_pose(snd, ai[0], (long)ctx->utm_zone());
                    measT *mt = trnx_utils::mb1_to_meas(snd, ai[0], trn_type[0], (long)ctx->utm_zone());

                    if(pt != nullptr && mt != nullptr){

                        double nav_time = ni->time_usec()/1e6;

                        // publish update TRN, publish estimate to TRN, LCM
                        ctx->pub_trn(nav_time, pt, mt, trn_type[0], xpp->pub_list(), cfg);
                    }

                    if(pt != nullptr)
                        delete pt;
                    if(mt != nullptr)
                        delete mt;
                }
            } else {
                TRN_NDPRINT(5, "%s:%d WARN - not ready count/mod[%d/%d]\n", __func__, __LINE__,ctx->cbcount(), ctx->decmod());
            }
            ctx->inc_cbcount();

            // write CSV
            // use sled bathy, vehicle attitude
            // TODO : make new method for mb1 bathy
            if(ctx->write_mb1_csv(snd, bi[0], ai[0], vi) > 0){
                cfg->stats().mb_csv_n++;
            }


            // release sounding memory
            mb1_destroy(&snd);
            retval=0;
        } else {
            TRN_NDPRINT(5, "%s:%d WARN - no beams\n", __func__, __LINE__);
        }

        if(bi[0] != nullptr)
            delete bi[0];
        if(ai[0] != nullptr)
            delete ai[0];
        if(ai[1] != nullptr)
            delete ai[1];
        if(ni != nullptr)
            delete ni;
        if(vi != nullptr)
            delete vi;
    }

    return retval;
}
