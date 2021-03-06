//#include "stdafx.h"
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <omp.h>
#include "ScSR.h"
#include "img_utils.h"

using namespace std;

/*
typedef struct _ParamScSR
{
       double up_scale, lambda, overlap;
       double *Dh, *Dl, *DlTxDl;
	   int Dhw, Dhh, Dlw, Dlh;
       int patch_size, maxIter; 

}ParamScSR;
*/
void copy_gray_image_d( const double *pSrc, int &src_imgw, int &src_imgh, int &start_x, int &start_y, double *pDst, int &dst_imgw, int &dst_imgh );
double sum_of_product( double *vector_a, double *vector_b, int vector_length );
void set_patch_image( double *pSrc, int *labelMtx, int &src_imgw, int &src_imgh, int &start_x, int &start_y, double *pDst, int &dst_imgw, int &dst_imgh );						 
//% first order gradient filters
double hf1[] = {-1,0,1};
double vf1[] = {-1,0,1};
 
//% second order gradient filters
double hf2[] = {1,0,-2,0,1};
double vf2[] = {1,0,-2,0,1};

  
void extr_lIm_fea( double *lIm, double *lImFea, int &nrow, int &ncol  )
{

	//lImFea(:, :, 1) = conv2(lIm, hf1, 'same');
	//lImFea(:, :, 2) = conv2(lIm, vf1, 'same');
	//lImFea(:, :, 3) = conv2(lIm,hf2,'same');
	//lImFea(:, :, 4) = conv2(lIm,vf2,'same');

	double *ptr1=lImFea;
	double *ptr2=lImFea+nrow*ncol;	
	double *ptr3=lImFea+2*nrow*ncol;	
	double *ptr4=lImFea+3*nrow*ncol;
	// need border image
	convolve2DSeparable( lIm, ptr1, ptr2, ncol, nrow, hf1, 3, vf1, 3 );
	convolve2DSeparable( lIm, ptr3, ptr4, ncol, nrow, hf2, 5, vf2, 5 );


}

bool ScSR( unsigned char *im_l_y, int &nrow, int &ncol, ParamScSR &strParamScSR, double *hIm )
{
	// % bicubic interpolation of the low-resolution image
	// mIm = single(imresize(lIm, up_scale, 'bicubic'));
	// hIm = zeros(size(mIm));
	// cntMat = zeros(size(mIm));
	// [h, w] = size(mIm);
	// % extract low-resolution image features
	// lImfea = extr_lIm_fea(mIm);

	int i, j, ii, jj, nrowx2, ncolx2;
	unsigned char *byte_mIm;
	double *mIm, *lImfea;
	double *DlT;//, *hIm;
	int *cntMat;
	DlT = new double[strParamScSR.Dlw*strParamScSR.Dlh];
    
	// transpose
	for( i=0; i<strParamScSR.Dlh; i++ )
	{
		ii=i*strParamScSR.Dlw;
		for( j=0; j<strParamScSR.Dlw; j++ ){
			DlT[j*strParamScSR.Dlh+i] = strParamScSR.Dl[ii+j];
		}
	}

	nrowx2 = (nrow<<1);
	ncolx2 = (ncol<<1);
	byte_mIm = new unsigned char[nrowx2*ncolx2];
	mIm = new double[nrowx2*ncolx2];
	lImfea = new double[nrowx2*ncolx2*4];
	if(!byte_mIm || !mIm || !lImfea){
		printf("Error: No memory!\n" );
	}
	resize_image_bau( im_l_y, byte_mIm, nrow, ncol, nrowx2, ncolx2 ); 
	
	//write_pgm_y( "im_l_y_scale2.pgm", nrowx2, ncolx2, byte_mIm );

	//hIm = new double[nrowx2*ncolx2];
	cntMat = new int[nrowx2*ncolx2];
	memset( hIm, 0, sizeof(double)*nrowx2*ncolx2 );
	memset( cntMat, 0, sizeof(int)*nrowx2*ncolx2 );

#pragma omp parallel for private(byte_mIm)
	for( i=0; i<(nrowx2*ncolx2); i++ )
	{
		mIm[i] = (double)byte_mIm[i];
	}


	/* read from raw data !!*/
	/*
	FILE* fp=fopen("mIm_256x256.dat", "rb");
	fread(mIm, sizeof(double), 256*256, fp);
	fclose(fp);
	*/

	// % extract low-resolution image features	
	extr_lIm_fea( mIm, lImfea, nrowx2, ncolx2 );

	/*
	fp = fopen("lImfea_1x256x256x4.dat", "rb");
	fread(lImfea, sizeof(double), 256*256*4, fp);
	fclose(fp);
	*/
	
	//% patch indexes for sparse recovery (avoid boundary)
	//gridx = 3:patch_size - overlap : w-patch_size-2;
	//gridx = [gridx, w-patch_size-2];
	//gridy = 3:patch_size - overlap : h-patch_size-2;
	//gridy = [gridy, h-patch_size-2];	
	int *gridx = new int[ncolx2];
	int *gridy = new int[nrowx2];
	memset( gridx, 0, sizeof(int)*ncolx2 );
	memset( gridy, 0, sizeof(int)*nrowx2 );
	int step = strParamScSR.patch_size - strParamScSR.overlap;
	//gridx[0]=3;
	//gridy[0]=3;
	int count_idx=0;
	for( i=3; i<=(ncolx2-strParamScSR.patch_size-2); i+=step ){
		gridx[count_idx] = i;	
		count_idx++;
	}
	int gridx_len = count_idx+1;
	gridx[count_idx] = ncolx2-strParamScSR.patch_size-2;
	
	count_idx=0;
	for( i=3; i<=(nrowx2-strParamScSR.patch_size-2); i+=step ){
		gridy[count_idx] = i;	
		count_idx++;
	}
	gridy[count_idx] = nrowx2-strParamScSR.patch_size-2;
	int gridy_len = count_idx;//+1;

#pragma omp parallel 
{
#pragma omp for collapse(2)
	for( ii=0; ii<gridx_len; ii++ )
	{
		for( jj=0; jj<gridy_len; jj++ )
		{

			dprintf("running ii=%d jj=%d\n", ii, jj);
			   
			int i, kk=0;
			//cnt = cnt+1;
			int xx = gridx[ii]-1;
			int yy = gridy[jj]-1;
			double *b, *L1QP_w, *hPatch, *mPatch, *mPatchFea ;

			b = new double[strParamScSR.Dlw];
			L1QP_w = new double[strParamScSR.Dhh*strParamScSR.Dhw];
			hPatch = new double[strParamScSR.Dhh];
			mPatch = new double[nrow*ncol];
			mPatchFea = new double[nrow*ncol];

			//mPatch = mIm(yy:yy+patch_size-1, xx:xx+patch_size-1);
			//mMean = mean(mPatch(:));
			//mPatch = mPatch(:) - mMean;
			//mNorm = sqrt(sum(mPatch.^2));
			int pxp = strParamScSR.patch_size*strParamScSR.patch_size;

			copy_gray_image_d( mIm, ncolx2, nrowx2, xx, yy, 
				mPatch, strParamScSR.patch_size, strParamScSR.patch_size );

			double mean_val=0, mNorm=0, mfNorm=0;
			for( kk=0; kk<pxp; kk++ ){
				mean_val+=mPatch[kk];
			}


			mean_val/=pxp;
			for( kk=0; kk<pxp; kk++ ){
				mPatch[kk]-=mean_val;
				mNorm+=pow(mPatch[kk], 2);
			}
			mNorm = sqrt(mNorm);


			//mPatchFea = lImfea(yy:yy+patch_size-1, xx:xx+patch_size-1, :);   
			//mPatchFea = mPatchFea(:);
			//mfNorm = sqrt(sum(mPatchFea.^2));
			//if mfNorm > 1,
			//	y = mPatchFea./mfNorm;
			//else
			//	y = mPatchFea;
			//end

			for( kk=0; kk<4; kk++ )
			{
				copy_gray_image_d( lImfea+kk*(ncolx2*nrowx2), ncolx2, nrowx2, xx, yy, 
					mPatchFea+kk*pxp, strParamScSR.patch_size, strParamScSR.patch_size );
			}


			for( kk=0; kk<4*pxp; kk++ )
			{
				mfNorm += (mPatchFea[kk]*mPatchFea[kk]);	
			}

			mfNorm = sqrt(mfNorm);
			if(mfNorm>1)
			{
				double mfNorm2 = 1.0/mfNorm;
				{
					for( kk=0; kk<4*pxp; kk++ ){
						//mPatchFea[kk] /= mfNorm;	
						mPatchFea[kk] *= mfNorm2;	
					}
				}
			}

			//b = -Dl'*y;
	  //    
			//% sparse recovery
			//w = L1QP_FeatureSign_yang(lambda, A, b);
	  //      
			//% generate the high resolution patch and scale the contrast
			//hPatch = Dh*w;
			//hPatch = lin_scale(hPatch, mNorm);
	  //      
			//hPatch = reshape(hPatch, [patch_size, patch_size]);
			//hPatch = hPatch + mMean;
	  //      
			//hIm(yy:yy+patch_size-1, xx:xx+patch_size-1) = hIm(yy:yy+patch_size-1, xx:xx+patch_size-1) + hPatch;
			//cntMat(yy:yy+patch_size-1, xx:xx+patch_size-1) = cntMat(yy:yy+patch_size-1, xx:xx+patch_size-1) + 1;

			memset(b, 0, sizeof(double)*1024);
			for( i=0; i<strParamScSR.Dlw; i++ ){
				b[i] = -sum_of_product( strParamScSR.Dl+i*strParamScSR.Dlh, mPatchFea, strParamScSR.Dlh ); 
			}
			//dprintf("ii=%d jj=%d\n", ii, jj);
			//% sparse recovery
			//w = L1QP_FeatureSign_yang(lambda, A, b);
			int d_size = strParamScSR.Dlw;

			memset(L1QP_w, 0, sizeof(double)*strParamScSR.Dhh*strParamScSR.Dhw);
			L1QP_FeatureSign_yang(strParamScSR.lambda, strParamScSR.DlTxDl, b, L1QP_w, d_size );

			for( i=0; i<strParamScSR.Dhh; i++ ){
				hPatch[i] = sum_of_product( strParamScSR.Dh+i*strParamScSR.Dhw, L1QP_w, strParamScSR.Dhw );  
			}


			// lin_scale
			double hNorm=0,s;
			for( i=0; i<strParamScSR.Dhh; i++ ){
				hNorm+=(hPatch[i]*hPatch[i]);
			}
			hNorm = sqrt(hNorm);

			if(hNorm!=0){
				s=mNorm*1.2/hNorm;
				for( i=0; i<strParamScSR.Dhh; i++ ){
					hPatch[i] *= s;
				}
			}
			for( i=0; i<strParamScSR.Dhh; i++ ){
				hPatch[i] += mean_val;
			}
			
			set_patch_image( hIm, cntMat, ncolx2, nrowx2, xx, yy, 
				hPatch, strParamScSR.patch_size, strParamScSR.patch_size );

			delete[]b;
			delete[]L1QP_w;
			delete[]hPatch;
			delete[]mPatch;
			delete[]mPatchFea;

		}
	}
}

	for( i=0; i<(ncolx2*nrowx2); i++ )
	{
		if(cntMat[i]<1){
			hIm[i] = mIm[i];
			cntMat[i] = 1;
		}
		hIm[i]/=cntMat[i];
	}

	// cast to int8


	/*
	dprintf("writing image\n");
	unsigned char *pShowImage=new unsigned char[ncolx2*nrowx2];
	
	for( i=0;i<(ncolx2*nrowx2);i++ )
	{
		pShowImage[i] = hIm[i];
	}
	
	write_pgm_y( "pShowImage.pgm", ncolx2, ncolx2, pShowImage );
	
	
	delete[]pShowImage;
	*/

	delete[]gridx;
	delete[]gridy;
	delete[]byte_mIm;
	delete[]lImfea;
	delete[]mIm;
	delete[]DlT;
//	delete[]hIm;
	delete[]cntMat;


	return true;
}

double sum_of_product( double *vector_a, double *vector_b, int vector_length )
{
	double sum=0;
	int i;
	double *ptraa=vector_a;
	double *ptrbb=vector_b;

	for( i=0; i<vector_length; i++ )
	{
		sum += (*ptraa * *ptrbb);
		ptraa++;
		ptrbb++;
	}

	return sum;
}

void set_patch_image( double *pSrc, int *labelMtx, int &src_imgw, int &src_imgh, int &start_x, int &start_y, double *pDst, int &dst_imgw, int &dst_imgh )
{

#if 0
	int i, x, y, src_img_dim, end_x, end_y, count;
	end_x = start_x + dst_imgw;
	end_y = start_y + dst_imgh;

	count = 0;

	/*i = 0;
	for( y=0; y<src_imgh; y++ ) {
	for( x=0; x<src_imgw; x++ , i++) {
	if( x>=start_x && x<end_x && y>=start_y && y<end_y ) {
	pDst[count++] = pSrc[i];
	}
	}
	}//*/
	if( start_x<0 ){ start_x = 0; }
	if( start_y<0 ){ start_y = 0; }
	if( (start_x+dst_imgw)>src_imgw ){ dst_imgw = src_imgw - start_x; }
	if( (start_y+dst_imgh)>src_imgh ){ dst_imgh = src_imgh - start_y; }

	double *ptraa = (double*)pSrc + start_y*src_imgw + start_x;
	double *ptrbb = pDst;
	int *ptrcc = labelMtx + start_y*src_imgw + start_x;
	for( y=0; y<dst_imgh; y++ )
	{
		 
		for( x=0; x<dst_imgw; x++ ){
			ptraa[x]+=ptrbb[x];
			ptrcc[x] += 1; 
		}
		ptraa+=src_imgw;
		ptrbb+=dst_imgw;

		ptrcc += src_imgw;
	}

#else


    int i, j, ii, x, y, end_x, end_y, count;
    end_x = start_x + dst_imgw;
    end_y = start_y + dst_imgh;
 
    count = 0;

    if( start_x<0 ){ start_x = 0; }
    if( start_y<0 ){ start_y = 0; }
    if( (start_x+dst_imgw)>src_imgw ){ dst_imgw = src_imgw - start_x; }
    if( (start_y+dst_imgh)>src_imgh ){ dst_imgh = src_imgh - start_y; }

    double *new_dst=new double[dst_imgw*dst_imgh];
    for( i=0; i<dst_imgh; i++ )
    {
        ii=i*dst_imgw;
        for( j=0; j<dst_imgw; j++ )
        {
            new_dst[j*dst_imgh+i] = pDst[ii+j];   

        }
    }
 
    double *ptraa = (double*)pSrc + start_y*src_imgw + start_x;
    double *ptrbb = new_dst;//pDst;
    int *ptrcc = labelMtx + start_y*src_imgw + start_x;
    for( y=0; y<dst_imgh; y++ )
    {
         
        for( x=0; x<dst_imgw; x++ ){
            ptraa[x]+=ptrbb[x];
            ptrcc[x] += 1; 
        }
        ptraa+=src_imgw;
        ptrbb+=dst_imgw;
 
        ptrcc += src_imgw;
    }
 
    delete[]new_dst; 

#endif
}
void copy_gray_image( const unsigned char *pSrc, int &src_imgw, int &src_imgh, int &start_x, int &start_y, unsigned char *pDst, int &dst_imgw, int &dst_imgh )
{
	int y, end_x, end_y, count;
	end_x = start_x + dst_imgw;
	end_y = start_y + dst_imgh;

	count = 0;

	/*i = 0;
	for( y=0; y<src_imgh; y++ ) {
	for( x=0; x<src_imgw; x++ , i++) {
	if( x>=start_x && x<end_x && y>=start_y && y<end_y ) {
	pDst[count++] = pSrc[i];
	}
	}
	}//*/
	if( start_x<0 ){ start_x = 0; }
	if( start_y<0 ){ start_y = 0; }
	if( (start_x+dst_imgw)>src_imgw ){ dst_imgw = src_imgw - start_x; }
	if( (start_y+dst_imgh)>src_imgh ){ dst_imgh = src_imgh - start_y; }

	unsigned char *ptraa = (unsigned char*)pSrc + start_y*src_imgw + start_x;
	unsigned char *ptrbb = pDst;
	for( y=0; y<dst_imgh; y++ )
	{
		memcpy( ptrbb, ptraa, dst_imgw ); 
		ptraa+=src_imgw;
		ptrbb+=dst_imgw;
	}


}

void copy_gray_image_d( const double *pSrc, int &src_imgw, int &src_imgh, int &start_x, int &start_y, double *pDst, int &dst_imgw, int &dst_imgh )
{


	int x, y, yy, end_x, end_y, count;
	end_x = start_x + dst_imgw;
	end_y = start_y + dst_imgh;

	count = 0;

	/*i = 0;
	for( y=0; y<src_imgh; y++ ) {
	for( x=0; x<src_imgw; x++ , i++) {
	if( x>=start_x && x<end_x && y>=start_y && y<end_y ) {
	pDst[count++] = pSrc[i];
	}
	}
	}*/
	if( start_x<0 ){ start_x = 0; }
	if( start_y<0 ){ start_y = 0; }
	if( (start_x+dst_imgw)>src_imgw ){ dst_imgw = src_imgw - start_x; }
	if( (start_y+dst_imgh)>src_imgh ){ dst_imgh = src_imgh - start_y; }

	/*
	double *ptraa = (double*)pSrc + start_y*src_imgw + start_x;
	double *ptrbb = pDst;
	for( y=0; y<dst_imgh; y++ )
	{
		memcpy( ptrbb, ptraa, sizeof(double)*dst_imgw ); 
		ptraa+=src_imgw;
		ptrbb+=dst_imgw;
	}
	*/

	double *new_dst=new double[dst_imgw*dst_imgh];
	
	double *ptraa = (double*)pSrc + start_y*src_imgw + start_x;
	double *ptrbb = new_dst;
	for( y=0; y<dst_imgh; y++ )
	{
		memcpy( ptrbb, ptraa, sizeof(double)*dst_imgw ); 
		ptraa+=src_imgw;
		ptrbb+=dst_imgw;
	}
	
	
	for( y=0; y<dst_imgh; y++ )
	{
		yy=y*dst_imgw;
		for( x=0; x<dst_imgw; x++ )
		{	
			pDst[x*dst_imgh+y] = new_dst[yy+x];		
		}
	}
	delete[]new_dst;

}

