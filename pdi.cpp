
// This is NWNC (No Warranty No Copyright) Software.
// astar.ai
// May 15, 2019

#include <GL/gl.h>
#include <opencv2/opencv.hpp>

#include "glwindow/scenewindow.hpp"

////////////////////////////////////////////////////////////////////////////////

bool      live = false;
//To run 'live = true' mode, you need a CaliCam from www.astar.ai

int       rect_cols = 640, rect_rows = 480;
int       ndisp_bar =  1,  wsize_bar = 2, thr_bar = 0;
int       ndisp_max =  2,  wsize_max = 3, thr_max = 120;
int       ndisp_now = 32,  wsize_now = 9, thr_now = 200;

int       cap_cols, cap_rows, img_width;
double    xi;

cv::Mat   Translation, Kl, Kr, Dl, Dr, xil, xir, Rl, Rr,
          mmap[2], smap[2][2], Kfe;

std::string cam_model;

glwindow::SceneWindow* scene;

////////////////////////////////////////////////////////////////////////////////

void OnTrackNdisp(int, void*) {
  ndisp_now = 16 + 16 * ndisp_bar;
}

////////////////////////////////////////////////////////////////////////////////

void OnTrackWsize(int, void*) {
  wsize_now = 5 + 2 * wsize_bar;
}

////////////////////////////////////////////////////////////////////////////////

void OnTrackThreshold(int, void*) {
  thr_now = 200 + thr_bar;
}

////////////////////////////////////////////////////////////////////////////////

void LoadParameters(std::string file_name) {
  cv::FileStorage fs(file_name, cv::FileStorage::READ);
  if (!fs.isOpened()) {
    std::cout << "Failed to open ini parameters" << std::endl;
    exit(-1);
  }

  cv::Size cap_size;
  fs["cam_model"] >> cam_model;
  fs["cap_size" ] >> cap_size;
  fs["Kl"       ] >> Kl;
  fs["Dl"       ] >> Dl;
  fs["xil"      ] >> xil;
  Rl = cv::Mat::eye(3, 3, CV_64F);

  if (cam_model == "stereo") {
    fs["Rl"       ] >> Rl;
    fs["Kr"       ] >> Kr;
    fs["Dr"       ] >> Dr;
    fs["xir"      ] >> xir;
    fs["Rr"       ] >> Rr;
    fs["T"        ] >> Translation;
  }
  fs.release();

  img_width = cap_size.width;
  cap_cols  = cap_size.width;
  cap_rows  = cap_size.height;

  if (cam_model == "stereo")
    img_width  = cap_size.width / 2;
}

////////////////////////////////////////////////////////////////////////////////

enum RecMode {
  REC_PERSPECTIVE,
  REC_FISHEYE,
  REC_LONGLAT
};

inline double MatRowMul(const cv::Mat& m, double x, double y, double z, int r) {
  return m.at<double>(r,0) * x + m.at<double>(r,1) * y + m.at<double>(r,2) * z;
}

void InitRectifyMap(cv::Mat K, cv::Mat D, double xi0, cv::Mat R,
                    cv::Mat Knew, double xi1, cv::Size size,
                    cv::Mat& map1, cv::Mat& map2, RecMode mode) {
  map1.create(size, CV_32F);
  map2.create(size, CV_32F);

  double fx = K.at<double>(0,0);
  double fy = K.at<double>(1,1);
  double cx = K.at<double>(0,2);
  double cy = K.at<double>(1,2);
  double s  = K.at<double>(0,1);

  double k1 = D.at<double>(0,0);
  double k2 = D.at<double>(0,1);
  double p1 = D.at<double>(0,2);
  double p2 = D.at<double>(0,3);

  cv::Mat Ki  = Knew.inv();
  cv::Mat Ri  = R.inv();
  cv::Mat KRi = (Knew * R).inv();

  for (int r = 0; r < size.height; ++r) {
    for (int c = 0; c < size.width; ++c) {
      double xc = 0.;
      double yc = 0.;
      double zc = 0.;

      if (mode == REC_PERSPECTIVE) {
        xc = MatRowMul(KRi, c, r, 1., 0);
        yc = MatRowMul(KRi, c, r, 1., 1);
        zc = MatRowMul(KRi, c, r, 1., 2);
      }

      if (mode == REC_LONGLAT) {
        double tt = MatRowMul(Ki, c, r, 1., 0); // w/pi
        double pp = MatRowMul(Ki, c, r, 1., 1); // h/pi

        double xn = -cos(tt);
        double yn = -sin(tt) * cos(pp);
        double zn =  sin(tt) * sin(pp);

        xc = MatRowMul(Ri, xn, yn, zn, 0);
        yc = MatRowMul(Ri, xn, yn, zn, 1);
        zc = MatRowMul(Ri, xn, yn, zn, 2);
      }

      if (mode == REC_FISHEYE) {
        double ee = MatRowMul(Ki, c, r, 1., 0);
        double ff = MatRowMul(Ki, c, r, 1., 1);

        double ef = ee * ee + ff * ff;
        double zz = (xi1 + sqrt(1. + (1. - xi1 * xi1) * ef)) / (ef + 1.);

        double xn = zz * ee;
        double yn = zz * ff;
        double zn = zz - xi1;

        xc = MatRowMul(Ri, xn, yn, zn, 0);
        yc = MatRowMul(Ri, xn, yn, zn, 1);
        zc = MatRowMul(Ri, xn, yn, zn, 2);
      }

      double rr = sqrt(xc * xc + yc * yc + zc * zc);
      double xs = xc / rr;
      double ys = yc / rr;
      double zs = zc / rr;

      double xu = xs / (zs + xi0);
      double yu = ys / (zs + xi0);

      double r2 = xu * xu + yu * yu;
      double r4 = r2 * r2;
      double xd = (1+k1*r2+k2*r4)*xu + 2*p1*xu*yu + p2*(r2+2*xu*xu);
      double yd = (1+k1*r2+k2*r4)*yu + 2*p2*xu*yu + p1*(r2+2*yu*yu);

      double u = fx * xd + s * yd + cx;
      double v = fy * yd + cy;

      map1.at<float>(r,c) = (float) u;
      map2.at<float>(r,c) = (float) v;
    }
  }
}

////////////////////////////////////////////////////////////////////////////////

void InitRectifyMap() {
  Kfe = cv::Mat::eye(3, 3, CV_64F);
  Kfe.at<double>(0,0) = Kl.at<double>(0,0) * 0.4;
  Kfe.at<double>(0,2) = rect_cols  / 2. - 0.5;
  Kfe.at<double>(1,1) = Kfe.at<double>(0,0);
  Kfe.at<double>(1,2) = rect_rows / 2. - 0.5;

  xi = xil.at<double>(0,0);
  cv::Size img_size(rect_cols, rect_rows);
  InitRectifyMap(Kl, Dl, xil.at<double>(0,0), Rl, Kfe, xil.at<double>(0,0),
                 img_size, mmap[0], mmap[1], REC_FISHEYE);

  cv::Mat Kll = cv::Mat::eye(3, 3, CV_64F);
  Kll.at<double>(0,0) = img_size.width  / M_PI;
  Kll.at<double>(1,1) = img_size.height / M_PI;

  InitRectifyMap(Kl, Dl, xil.at<double>(0,0), Rl, Kll, 0.,
                 img_size, smap[0][0], smap[0][1], REC_LONGLAT);
  InitRectifyMap(Kr, Dr, xir.at<double>(0,0), Rr, Kll, 0.,
                 img_size, smap[1][0], smap[1][1], REC_LONGLAT);
}

////////////////////////////////////////////////////////////////////////////////

void DisparityImage(const cv::Mat& recl, const cv::Mat& recr, cv::Mat& disp) {
  cv::Mat disp16s;
  int N = ndisp_now, W = wsize_now, C = recl.channels();
  if (1) {
    cv::Ptr<cv::StereoSGBM> sgbm =
        cv::StereoSGBM::create(0, N, W, 8 * C * W * W, 32 * C * W * W);
    sgbm->compute(recl, recr, disp16s);
  } else {
    cv::Mat grayl, grayr;
    cv::cvtColor(recl, grayl, CV_BGR2GRAY);
    cv::cvtColor(recr, grayr, CV_BGR2GRAY);

    cv::Ptr<cv::StereoBM> sbm = cv::StereoBM::create(N, W);
    sbm->setPreFilterCap(31);
    sbm->setMinDisparity(0);
    sbm->setTextureThreshold(10);
    sbm->setUniquenessRatio(15);
    sbm->setSpeckleWindowSize(100);
    sbm->setSpeckleRange(32);
    sbm->setDisp12MaxDiff(1);
    sbm->compute(grayl, grayr, disp16s);
  }

  disp16s.convertTo(disp, CV_32F, 1.f / 16.f);
}

////////////////////////////////////////////////////////////////////////////////

void DrawScene(const cv::Mat feim, const cv::Mat disp_img) {
  glBegin(GL_POINTS);

  double fx = Kfe.at<double>(0,0);
  double fy = Kfe.at<double>(1,1);
  double cx = Kfe.at<double>(0,2);
  double cy = Kfe.at<double>(1,2);
  double bl = cv::norm(Translation);

  double w_pi = rect_cols  / M_PI;
  double h_pi = rect_rows / M_PI;
  double pi_w = M_PI / rect_cols;

  for (int r = 0; r < feim.rows; ++r) {
    for (int c = 0; c < feim.cols; ++c) {
      cv::Vec3b color = feim.at<cv::Vec3b>(r,c);
      if (hypot(c - cx, r - cy) > thr_now)
        continue;

      double e = (c - cx) / fx;
      double f = (r - cy) / fy;
      double ef = e * e + f * f;
      double zx = (xi + sqrt(1. + (1. - xi*xi) * ef)) / (ef + 1.);

      if (isnan(zx))
        continue;

      double xc = e * zx;
      double yc = f * zx;
      double zc = zx - xi;

      double tt = acos(-xc);
      double pp = acos(-yc / hypot(yc, zc));
      float disp = disp_img.at<float>((int) (pp * h_pi), (int) (tt * w_pi));

      if (disp <= 0.f)
        continue;

      double diff  = pi_w * disp;
      double depth = bl * sin(tt - diff) / sin(diff);

      glColor3ub(color(2), color(1), color(0));
      glVertex3f(xc * depth, yc * depth, zc * depth);
    }
  }

  glEnd();
}

////////////////////////////////////////////////////////////////////////////////

int main(int argc, char** argv) {
  std::string param_name = argc >= 2 ? argv[1] : "./astar_calicam.yml";
  std::string image_name = argc == 3 ? argv[2] : "./wm_garden.jpg";

  LoadParameters(param_name);
  InitRectifyMap();

  scene = new glwindow::SceneWindow(960, 720, "Panorama 3D Scene");

  cv::VideoCapture vcapture;
  if (live) {
    vcapture.open(0);

    if (!vcapture.isOpened()) {
      std::cout << "Camera doesn't work" << std::endl;
      exit(-1);
    }

    vcapture.set(CV_CAP_PROP_FRAME_WIDTH,  cap_cols);
    vcapture.set(CV_CAP_PROP_FRAME_HEIGHT, cap_rows);
    vcapture.set(CV_CAP_PROP_FPS, 30);
  }

  std::string win_name = "Fisheye Image";
  cv::namedWindow(win_name);

  if (cam_model == "stereo") {
    cv::createTrackbar("Num Disp:  16 + 16 *", win_name,
                       &ndisp_bar,  ndisp_max,   OnTrackNdisp);
    cv::createTrackbar("Blk   Size :     5  +  2 *", win_name,
                       &wsize_bar,  wsize_max,  OnTrackWsize);
    cv::createTrackbar("Threshold :     200 + ", win_name,
                       &thr_bar,    thr_max,    OnTrackThreshold);
  }

  cv::Mat raw_img = cv::imread(image_name, cv::IMREAD_COLOR);
  cv::Mat raw_imgl, raw_imgr, fe_img, ll_imgl, ll_imgr;
  while (1) {
    if (live)
      vcapture >> raw_img;

    if (raw_img.total() == 0) {
      std::cout << "Image capture error" << std::endl;
      exit(-1);
    }

    raw_img(cv::Rect(        0, 0, img_width, cap_rows)).copyTo(raw_imgl);
    raw_img(cv::Rect(img_width, 0, img_width, cap_rows)).copyTo(raw_imgr);

    cv::remap(raw_imgl, ll_imgl, smap[0][0], smap[0][1], cv::INTER_LINEAR);
    cv::remap(raw_imgr, ll_imgr, smap[1][0], smap[1][1], cv::INTER_LINEAR);

    cv::remap(raw_imgl, fe_img, mmap[0], mmap[1], cv::INTER_LINEAR);

    imshow(win_name, fe_img);

    cv::Mat disp_img;
    DisparityImage(ll_imgl, ll_imgr, disp_img);

    if (scene->win.alive()) {
      if (scene->start_draw()) {
        DrawScene(fe_img, disp_img);
        scene->finish_draw();
      }
    }

    char key = cv::waitKey(1);
    if (key == 'q' || key == 'Q' || key == 27)
      break;
  }

  delete scene;

  return 0;
}

////////////////////////////////////////////////////////////////////////////////



