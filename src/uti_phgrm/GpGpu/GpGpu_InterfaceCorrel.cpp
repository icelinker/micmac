#include "GpGpu/GpGpu_InterCorrel.h"

/// \brief Constructeur GpGpuInterfaceCorrel
GpGpuInterfaceCorrel::GpGpuInterfaceCorrel():
    CSimpleJobCpuGpu(true),
     NoMasked(false),
     copyInvParam(false)

{
    for (int s = 0;s<NSTREAM;s++)
        checkCudaErrors( cudaStreamCreate(GetStream(s)));

    //CreateJob();

    freezeCompute();
}

GpGpuInterfaceCorrel::~GpGpuInterfaceCorrel()
{
    for (int s = 0;s<NSTREAM;s++)
        checkCudaErrors( cudaStreamDestroy(*(GetStream(s))));

}
void GpGpuInterfaceCorrel::ReallocHostData(uint interZ,ushort idBuff)
{
    _data2Cor.ReallocHostData(interZ,_param[idBuff],idBuff);
}

uint2 &GpGpuInterfaceCorrel::DimTerrainGlob()
{
    return _m_DimTerrainGlob;
}

std::vector<cellules> &GpGpuInterfaceCorrel::MaskVolumeBlock()
{
    return _m_MaskVolumeBlock;
}

uint GpGpuInterfaceCorrel::InitCorrelJob(int Zmin, int Zmax)
{

    uint interZ = min(INTERZ, abs(Zmin - Zmax));

    if(UseMultiThreading())
    {
        ResetIdBuffer();
        SetPreComp(true);
    }

    return interZ;
}

/// \brief Initialisation des parametres constants
void GpGpuInterfaceCorrel::SetParameter(int nbLayer , ushort2 dRVig , uint2 dimImg, float mAhEpsilon, uint samplingZ, int uvINTDef, ushort nClass )
{

    if(!copyInvParam || _param[0].invPC.nbImages != (uint)nbLayer || _param[1].invPC.nbImages != (uint)nbLayer)
    {
        copyInvParam = true;
        _param[0].invPC.SetParamInva( dRVig * 2 + 1,dRVig, dimImg, mAhEpsilon, samplingZ, uvINTDef, nbLayer,nClass);
        _param[1].invPC.SetParamInva( dRVig * 2 + 1,dRVig, dimImg, mAhEpsilon, samplingZ, uvINTDef, nbLayer,nClass);
        CopyParamInvTodevice(_param[0]);
    }
}

void GpGpuInterfaceCorrel::BasicCorrelation()
{

    // Re-allocation les structures de donn�es si elles ont �t� modifi�es

    Data().ReallocDeviceData(Param(GetIdBuf()));

    // copie des donnees du host vers le device

    Data().copyHostToDevice(Param(GetIdBuf()));

    // Indique que la copie est termin�e pour le thread de calcul des projections
    SetPreComp(true);

    // COPIE Les parametres � Virer!!
    //CopyParamTodevice(_param[GetIdBuf()]);

    //Param(GetIdBuf()).CopyParamToDevice();

    // Lancement du calcul de correlation
    CorrelationGpGpu(GetIdBuf());

    // relacher la texture de projection

    Data().UnBindTextureProj();

    // Lancement du calcul de multi-correlation
    MultiCorrelationGpGpu(GetIdBuf());

    // Copier les resultats de calcul des couts du device vers le host!

    Data().CopyDevicetoHost(GetIdBuf());

}

cudaStream_t* GpGpuInterfaceCorrel::GetStream( int stream )
{
    return &(_stream[stream]);
}

void GpGpuInterfaceCorrel::simpleJob()
{
    boost::thread tOpti(&GpGpuInterfaceCorrel::oneCompute,this);
    tOpti.detach();
}

void GpGpuInterfaceCorrel::oneCompute()
{
    //uint interZ = Param(GetIdBuf()).ZCInter;

//    cout << "START CORREL :" << boost::this_thread::get_id() << endl;


    while(!GetCompute())
    {
        //printf("WAIT COMPUTE OPTIM...\n");
        boost::this_thread::sleep(boost::posix_time::microsec(1));
    }

    SetCompute(false);

    BasicCorrelation();

    while(GetDataToCopy());
//    {
//        printf("WAIT DATA COPY OPTIM...\n");
//        boost::this_thread::sleep(boost::posix_time::microsec(5));
//    }

    SwitchIdBuffer();
    SetDataToCopy(true);

    SetCompute(true);
    //cout << "END CORREL   :" << boost::this_thread::get_id() << endl;

}

void GpGpuInterfaceCorrel::threadCompute()
{
    ResetIdBuffer();
    while (true)
    {
        if (GetCompute())
        {


            // TEMP : TENTATIVE DE DEBUGAGE THREAD
            //while(Param(GetIdBuf()).invPC.nbImages > 4096)
            while(!Param(GetIdBuf()).HdPc.sizeCachAll)
                boost::this_thread::sleep(boost::posix_time::microsec(1));

            oneCompute();
        }
        else
            boost::this_thread::sleep(boost::posix_time::microsec(1));
    }
}

void GpGpuInterfaceCorrel::freezeCompute()
{
    Param(0).HdPc.sizeCachAll = 0;
    Param(1).HdPc.sizeCachAll = 0;
    SetDataToCopy(false);
    SetCompute(false);
    SetPreComp(false);
    //KillJob();
}

void GpGpuInterfaceCorrel::IntervalZ(uint &interZ, int anZProjection, int aZMaxTer)
{
    uint intZ = (uint)abs(aZMaxTer - anZProjection );
    if (interZ >= intZ  &&  anZProjection != (aZMaxTer - 1) )
        interZ = intZ;
}

float *GpGpuInterfaceCorrel::VolumeCost(ushort id)
{
    return UseMultiThreading() ? Data().HostVolumeCost(id) : Data().HostVolumeCost(0);
}

bool GpGpuInterfaceCorrel::TexturesAreLoaded()
{
    return _TexturesAreLoaded;
}

void GpGpuInterfaceCorrel::SetTexturesAreLoaded(bool load)
{
    _TexturesAreLoaded = load;
    NoMasked = false;
}

void GpGpuInterfaceCorrel::CorrelationGpGpu(ushort idBuf,const int s )
{
    LaunchKernelCorrelation(s, *(GetStream(s)),_param[idBuf], _data2Cor);
    //LaunchKernelCorrelationZ(s,_param[idBuf],_data2Cor);
}

void GpGpuInterfaceCorrel::MultiCorrelationGpGpu(ushort idBuf, const int s)
{
    LaunchKernelMultiCorrelation( *(GetStream(s)),_param[idBuf],  _data2Cor);
}

pCorGpu& GpGpuInterfaceCorrel::Param(ushort idBuf)
{
    return _param[idBuf];
}

void GpGpuInterfaceCorrel::signalComputeCorrel(uint dZ)
{
    SetPreComp(false);
    SetCompute(true);
}

SData2Correl &GpGpuInterfaceCorrel::Data()
{
    return _data2Cor;
}
