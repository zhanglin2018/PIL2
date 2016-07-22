#include "System.h"
#include <base/Svar/Svar.h>
#include <base/Svar/Scommand.h>
#include <opencv2/highgui/highgui.hpp>
#include "VideoCompare.h"
#include "VideoFrame.h"
#include <base/Time/Global_Timer.h>

void SystemHandle(void *ptr,string cmd,string para)
{
    if(cmd=="System.Start")
    {
        svar.GetInt("PathSetted")=1;
        System* sys=(System*)ptr;
        if(!sys->isRunning())
            sys->start();
    }
    if(cmd=="System.Stop")
    {
        svar.GetInt("PathSetted")=0;
        System* sys=(System*)ptr;
        sys->stop();
    }


}

System::System()
{
    scommand.RegisterCommand("System.Start",SystemHandle,this);
    scommand.RegisterCommand("System.Stop",SystemHandle,this);
    if(svar.GetInt("WithQt"))
    {
        mainWindow=SPtr<MainWindow>(new MainWindow());
        mainWindow->call("Show");
    }
}


System::~System()
{
    stop();
    if(isRunning())
    {
        sleep(10);
    }
    join();
}

void System::run()
{

    if(svar.GetInt("WithQt"))
    {
        int& pathSetted=svar.GetInt("PathSetted");
        while(!pathSetted) sleep(10);
    }

    std::string& refVideoPath=svar.GetString("VideoRef.VideoPath","");
    if(!refVideoPath.size()){
        cerr<<"Please set the \"VideoRef.VideoPath\"!\n";
        return;
    }

    // load vocabulary
    string vocFile=svar.GetString("ORBVocabularyFile","");
    if(vocFile.empty())
    {
        cerr<<"Please set 'ORBVocabularyFile'.\n";
        return;
    }

    cout <<"Loading vocabulary: " << vocFile << " ... ";

    BOW_Object::mpORBvocabulary=SPtr<ORBVocabulary>(new ORBVocabulary);
    BOW_Object::mpORBvocabulary->loadFast(vocFile);
    cout << endl;

    // videoref from video or file
    std::string videoRefPath=svar.GetString("VideoRef.DataPath",refVideoPath+".ref");
    videoRef=SPtr<VideoRef>(new VideoRef(videoRefPath));
    if(!videoRef->loadFromFile(videoRefPath))
    {
        pi::timer.enter("TrainTotal");
        trainVideoRef(*videoRef,refVideoPath);
        pi::timer.leave("TrainTotal");
    }
    else
    {
        cout<<"VideoRef loaded from "<<videoRefPath<<"."<<endl;
    }

    // compare video difference
    findVideoDiff(*videoRef,svar.GetString("Video2Compare",""));

    videoRef->save2File(videoRefPath);

    cout<<"System stoped.\n";
}

bool System::trainVideoRef(VideoRef& refVideo,const std::string& refVideoPath)
{
    cv::VideoCapture video(refVideoPath);
    if(!video.isOpened())
    {
        cerr<<"Can't open video "<<refVideoPath<<endl;
        return false;
    }
    VideoCompare compare(refVideo);
    cout<<"Training VideoRef from video "<<refVideoPath<<".\n";

    pi::TicTac ticToc;
    ticToc.Tic();

    double scale=svar.GetDouble("ComputeScale",0.5);
    int    trainSkip=svar.GetInt("TrainSkip",5);
    int& pause=svar.GetInt("System.Pause");
    while(!shouldStop())
    {
        while(pause) sleep(10);
        SPtr<VideoFrame> frame(new VideoFrame);
        for(int i=0;i<trainSkip;i++)
            video>>frame->img;
        video>>frame->img;

        if(frame->img.empty())
        {
            break;
        }

        if(scale<1)
        {
            cv::resize(frame->img,frame->img,cv::Size(frame->img.cols*scale,frame->img.rows*scale));
        }

        pi::timer.enter("TrainFrame");
        compare.train(frame);
        pi::timer.leave("TrainFrame");

        if(!svar.GetInt("WithQt"))
        {
            if(!compare.refImg.empty()&&svar.GetInt("Draw.RefImage",1))
                cv::imshow("RefImage",compare.refImg);
            if(!compare.trackImg.empty()&&svar.GetInt("Draw.TrackImage",1))
                cv::imshow("TrackImage",compare.trackImg);
            if(!compare.refImgHere.empty()&&svar.GetInt("Draw.RefImageHere"))
                cv::imshow("RefImageHere",compare.refImgHere);
            if(!compare.warpImg.empty()&&svar.GetInt("Draw.WarpImage",1))
                cv::imshow("WarpImage",compare.warpImg);
            if(!compare.diffImg.empty()&&svar.GetInt("Draw.DiffImage",1))
                cv::imshow("DiffImage",compare.diffImg);
            uchar key=cv::waitKey(20);
            if(key==27)   stop();
        }
        else
        {
            mainWindow->call("Update");
        }
    }

    return true;
}

bool System::findVideoDiff(VideoRef& refVideo,const std::string& videoPath)
{
    if(videoPath.empty())
    {
        cerr<<"Video2Compare not setted. Skip comparison. \n";
        return false;
    }

    cv::VideoCapture video(videoPath);
    if(!video.isOpened())
    {
        cerr<<"Can't open video "<<videoPath<<endl;
        return false;
    }
    VideoCompare compare(refVideo);
    cout<<"Find difference at video "<<videoPath<<".\n";

    pi::TicTac ticToc;
    ticToc.Tic();

    double scale=svar.GetDouble("ComputeScale",0.5);
    int    computeSkip=svar.GetInt("ComputeSkip",5);

    int& pause=svar.GetInt("System.Pause");
    while(!shouldStop())
    {
        while(pause) sleep(10);
        SPtr<VideoFrame> frame(new VideoFrame);
        for(int i=0;i<computeSkip;i++)
            video>>frame->img;
        video>>frame->img;

        if(frame->img.empty())
        {
            break;
        }

        if(scale<1)
        {
            cv::resize(frame->img,frame->img,
                       cv::Size(frame->img.cols*scale,frame->img.rows*scale));
        }

        pi::timer.enter("TrackDiff");
        compare.track(frame);
        pi::timer.leave("TrackDiff");

        if(!svar.GetInt("WithQt"))
        {
            if(!compare.refImg.empty()&&svar.GetInt("Draw.RefImage",1))
                cv::imshow("RefImage",compare.refImg);
            if(!compare.trackImg.empty()&&svar.GetInt("Draw.TrackImage",1))
                cv::imshow("TrackImage",compare.trackImg);
            if(!compare.refImgHere.empty()&&svar.GetInt("Draw.RefImageHere"))
                cv::imshow("RefImageHere",compare.refImgHere);
            if(!compare.warpImg.empty()&&svar.GetInt("Draw.WarpImage",1))
                cv::imshow("WarpImage",compare.warpImg);
            if(!compare.diffImg.empty()&&svar.GetInt("Draw.DiffImage",1))
                cv::imshow("DiffImage",compare.diffImg);
            uchar key=cv::waitKey(20);
            if(key==27)   stop();
        }
        else
        {
            mainWindow->call("Update");
        }
    }

    return true;
}
