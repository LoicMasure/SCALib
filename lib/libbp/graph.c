#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <float.h>
#include <math.h>
#include <assert.h>
#include "tables.h"
#include "macro.h"
#include "utils.h"
#include "graph.h"
#include "graph_utils.h"
extern Vnode *vnodes;
extern Fnode *fnodes;
extern int32_t Nk;
extern int32_t *tab;

void update_vnode_log(Vnode *vnode){
    uint32_t i,j,fnode_id,r,Ni,Nf;
    Ni = vnode->Ni;
    Nf = vnode->Nf;
    proba_t *tmp1;

    tmp1 = (proba_t *) malloc(sizeof(proba_t)*Nk); // accumulate self distri + all messages
    apply_log10(tmp1,vnode->distri_orig,Nk);

    // Accumulate all in tmp1 as a log distri
    if(Ni > 0){
        // add all the functions that use this node
        apply_log10(fnodes[vnode->id_input].msg,fnodes[vnode->id_input].msg,Nk); 
        add_vec(tmp1,tmp1,fnodes[vnode->id_input].msg,Nk);
    }

    for(i=0;i<Nf;i++){
        fnode_id = vnode->id_output[i];
        r = vnode->relative[i];
        apply_log10(&(fnodes[fnode_id].msg[index(r,0,Nk)]),&(fnodes[fnode_id].msg[index(r,0,Nk)]),Nk);
        add_vec(tmp1,tmp1,&(fnodes[fnode_id].msg[index(r,0,Nk)]),Nk);
    }

    // add msg to input node if exists, substracts its contribution to tmp1
    if(Ni > 0){
        sub_vec(vnode->msg,tmp1,fnodes[vnode->id_input].msg,0,Nk);
        add_cst_dest(vnode->msg,vnode->msg,-get_max(vnode->msg,Nk),Nk);
        apply_P10(vnode->msg,vnode->msg,Nk);
        normalize_vec(vnode->msg,vnode->msg,Nk,1);
    }

    for(i=0;i<Nf;i++){
        proba_t *curr_msg = &(vnode->msg[index((Ni+i),0,Nk)]);
        fnode_id = vnode->id_output[i];
        r = vnode->relative[i];

        sub_vec(curr_msg,tmp1,&(fnodes[fnode_id].msg[index(r,0,Nk)]),0,Nk);
        add_cst_dest(curr_msg,curr_msg,-(get_max(curr_msg,Nk)),Nk);
        apply_P10(curr_msg,curr_msg,Nk);
        normalize_vec(curr_msg,curr_msg,Nk,1);
    }

    add_cst_dest(vnode->distri,tmp1,-get_max(tmp1,Nk),Nk);

    free(tmp1);
}

void update_vnode_information(Vnode *vnode){
    uint32_t i,j,fnode_id,r,Ni,Nf;
    Ni = vnode->Ni;
    Nf = vnode->Nf;

    proba_t total_sum= vnode->distri_orig[0];
    if(Ni > 0){
        total_sum += fnodes[vnode->id_input].msg[0];
    }
    for(i=0;i<Nf;i++){
        fnode_id = vnode->id_output[i];
        r = vnode->relative[i];
        total_sum += fnodes[fnode_id].msg[r];
    }

    vnode->distri[0] = min(total_sum,1.0);

    if(Ni > 0){
        vnode->msg[0] = min(total_sum - fnodes[vnode->id_input].msg[0],1.0);
    }
    for(i=0;i<Nf;i++){
        fnode_id = vnode->id_output[i];
        r = vnode->relative[i];
        vnode->msg[i+Ni] = min(total_sum - fnodes[fnode_id].msg[r],1.0);
    }
}

void update_vnode(Vnode *vnode){
    uint32_t i,j,fnode_id,r,Ni,Nf;
    if(vnode->use_log){
        return update_vnode_log(vnode);
    }
    Ni = vnode->Ni;
    Nf = vnode->Nf;

    for(i=0;i<((Nf+Ni)*Nk);i++)
        vnode->msg[i] = 1.0;
    // init the distri with original distri

    // compute to the function that outputed that variable
    // add msg from input node if exists
    if(Ni > 0){
        // add all the functions that use this node
        for(i=0;i<Nf;i++){
            fnode_id = vnode->id_output[i];
            r = vnode->relative[i];
            mult_vec(vnode->msg,vnode->msg,&(fnodes[fnode_id].msg[index(r,0,Nk)]),Nk);
        }
        mult_vec(vnode->msg,vnode->msg,vnode->distri_orig,Nk);
        normalize_vec(vnode->msg,vnode->msg,Nk,1);
    }

    for(i=0;i<Nf;i++){
        proba_t *curr_msg = &(vnode->msg[index((Ni+i),0,Nk)]);
        if(Ni>0)
            mult_vec(curr_msg,curr_msg,fnodes[vnode->id_input].msg,Nk);
        for(j=0;j<Nf;j++){
            if(i==j)
                continue;
            fnode_id = vnode->id_output[j];
            r = vnode->relative[j];
            mult_vec(curr_msg,curr_msg,&(fnodes[fnode_id].msg[index(r,0,Nk)]),Nk);
        }
        mult_vec(curr_msg,curr_msg,vnode->distri_orig,Nk);
        normalize_vec(curr_msg,curr_msg,Nk,1);
    }

    // compute all
    // add all the functions that use this node
    memcpy(vnode->distri,vnode->distri_orig,Nk*sizeof(proba_t));
    for(i=0;i<Nf;i++){
        fnode_id = vnode->id_output[i];
        r = vnode->relative[i];
        mult_vec(vnode->distri,vnode->distri,&(fnodes[fnode_id].msg[index(r,0,Nk)]),Nk);
    }
    if(Ni > 0){
        mult_vec(vnode->distri,vnode->distri,fnodes[vnode->id_input].msg,Nk);
    }
    normalize_vec(vnode->distri,vnode->distri,Nk,1);
}

void update_fnode_information(Fnode *fnode){
    uint32_t i,j;
    proba_t prod_all;

    // to the outputs nodes
    prod_all = 1.0;
    for(i=0;i<fnode->li;i++){
        Vnode vnodei = vnodes[fnode->i[i]];
        prod_all *= vnodei.msg[fnode->relative[i]];
    }
    fnode->msg[0] = min(prod_all,1.0);

    // to each input nodes
    for(i=0;i<fnode->li;i++){
        prod_all = vnodes[fnode->o].msg[0];
        for(j = 0;j<fnode->li;j++){
            if (i != j)
                prod_all *= vnodes[fnode->i[j]].msg[fnode->relative[j]];
        }
        fnode->msg[i+1] = min(prod_all,1.0);
    }
}


void update_fnode(Fnode *fnode){
    Vnode *vnode0,*vnode1,*vnodeO;
    uint32_t offset;
    uint32_t l0,l1;
    uint32_t i0,o,i;
    proba_t *distri0,*distri1,*distriO;


    memset(fnode->msg,0,sizeof(proba_t)*Nk*(fnode->li +1));
    // compute the distri of first input and output distribution
    vnode0 = &vnodes[fnode->i[0]];
    vnodeO = &vnodes[fnode->o];
    distriO = vnodeO->msg;
    distri0 = &(vnode0->msg[index(fnode->relative[0],0,Nk)]);

    if(distriO == NULL)
        exit(EXIT_FAILURE);
    if(distri0 == NULL)
        exit(EXIT_FAILURE);


    if(fnode->li == 2){ // 2 inputs function node

        // get the last input node messages
        vnode1 = &vnodes[fnode->i[1]];
        distri1 = &(vnode1->msg[index(fnode->relative[1],0,Nk)]);
        if(fnode->func_id == 2){ // XOR NODES
            xor_fwht(fnode->msg,distri0,distri1,distriO);
            tile(fnode->msg,fnode->msg,TILE,Nk);
        }
        else if(fnode->func_id == 0){ // AND Nodes
            and_ex(fnode->msg,distri0,distri1,distriO);
        }
        else
            exit(EXIT_FAILURE);
    }
    else if(fnode->li == 1){
        // iterate over the (single) sets of input
        for(i0=0;i0<Nk;i0++){
            if(fnode->func_id == 1)
                o = (~i0&0xffff);
            else if(fnode->func_id == 2 && fnode->has_offset)
                o = i0 ^ fnode->offset;
            else if(fnode->func_id == 3 && fnode->has_offset)
                o = ROL16(i0,fnode->offset);
            else if(fnode->func_id == 4){
                o = tab[index(fnode->offset,i0,Nk)];
            }else
                exit(EXIT_FAILURE);
            o &=0xffff;

            // update message to the output
            fnode->msg[index(0,o,Nk)] += (distri0[i0]);
            // update message to input 0
            fnode->msg[index(1,i0,Nk)] += (distriO[o]);
        }

    }else
        exit(EXIT_FAILURE);
    // tiling the data and normalizing
    for(l0=0;l0<(fnode->li+1);l0++){
        normalize_vec(&(fnode->msg[index(l0,0,Nk)]),&(fnode->msg[index(l0,0,Nk)]),Nk,1);
    }
    return;
}
