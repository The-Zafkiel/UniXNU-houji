#!/usr/bin/env python3
"""Exercise the actual status-bar adapter with host UIKit peers; no visual claim."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
s=(ROOT/'userland/display/UnifiedKeyboardGeometry.mm').read_text()
exact=s[s.index('bool exactType('):s.index('\n#include "HoujiStatusBarGeometry.h"')]
unit=r'''
#import <Foundation/Foundation.h>
#import <CoreGraphics/CGGeometry.h>
#import <objc/message.h>
#import <objc/runtime.h>
#include <algorithm>
#include <initializer_list>
#include <cmath>
#include <cstring>
#include <cassert>
struct Insets {CGFloat top,left,bottom,right;};
static void report(NSString *format,...) {va_list ap;va_start(ap,format);NSString *line=[[NSString alloc] initWithFormat:format arguments:ap];va_end(ap);fprintf(stderr,"%s\n",line.UTF8String);}
static bool target=true,expanded=false,external=false;
static NSInteger orientation=1;
static unsigned preparedBars=0,laidOutBars=0,writes=0,dataUpdates=0,avoidanceUpdates=0;
static id expectedData,expectedSettings;
static CGRect previousFrame;
@interface UIScreen:NSObject
+ (id)mainScreen;
- (CGRect)nativeBounds;
- (CGRect)bounds;
- (CGFloat)scale;
@end
@implementation UIScreen
+ (id)mainScreen {static id s=[UIScreen new];return s;}
- (CGRect)nativeBounds {return CGRectMake(0,0,target?1200:1170,2670);}
- (CGRect)bounds {return CGRectMake(0,0,400,890);}
- (CGFloat)scale {return 3;}
@end
@interface Window:NSObject
- (id)screen;
@end
@implementation Window
- (id)screen {return external?[UIScreen new]:[UIScreen mainScreen];}
@end
@interface UIFont:NSObject
- (CGFloat)capHeight;
@end
@implementation UIFont
- (CGFloat)capHeight {return 12;}
@end
@interface Constraint:NSObject
@property(nonatomic,strong) id firstItem,secondItem;
@property(nonatomic) NSInteger firstAttribute,secondAttribute,relation;
@property(nonatomic) CGFloat multiplier,constant;
@property(nonatomic,getter=isActive) BOOL active;
@end
@implementation Constraint
- (void)setConstant:(CGFloat)value {_constant=value;++writes;}
@end
@interface Container:NSObject
@property(nonatomic) CGRect frame;
@property(nonatomic,strong) NSArray *constraints;
@end
@implementation Container
@end
@interface Guide:NSObject
@property(nonatomic,weak) id owningView;
@property(nonatomic) CGRect layoutFrame;
@end
@implementation Guide
@end
@interface Region:NSObject
@property(nonatomic,strong) id layoutItem;
@end
@implementation Region
@end
@interface _UIStatusBarVisualProvider_DynamicSplit:NSObject
@property(nonatomic,strong) Guide *mainRegionsLayoutGuide;
@property(nonatomic,strong) Constraint *leadingCenterYConstraint;
@property(nonatomic,weak) id statusBar;
- (void)dataUpdated:(id)data;
- (void)avoidanceFrameUpdatedFromFrame:(CGRect)frame withAnimationSettings:(id)settings options:(NSUInteger)options;
- (BOOL)expanded;
- (id)normalFont;
@end
@implementation _UIStatusBarVisualProvider_DynamicSplit
- (void)dataUpdated:(id)data {assert(data==expectedData);++dataUpdates;self.leadingCenterYConstraint.constant=2;}
- (void)avoidanceFrameUpdatedFromFrame:(CGRect)frame withAnimationSettings:(id)settings options:(NSUInteger)options {
    assert(CGRectEqualToRect(frame,previousFrame) && settings==expectedSettings && options==0x13);
    ++avoidanceUpdates;self.leadingCenterYConstraint.constant=3;
}
- (BOOL)expanded {return expanded;}
- (id)normalFont {return [UIFont new];}
@end
@interface _UIStatusBarVisualProvider_DynamicSplitMedium:_UIStatusBarVisualProvider_DynamicSplit
@end
@implementation _UIStatusBarVisualProvider_DynamicSplitMedium
@end
@interface _UIStatusBar:NSObject
@property(nonatomic,strong) id visualProvider;
@property(nonatomic,strong) Container *containerView;
@property(nonatomic,strong) NSDictionary *regions;
@property(nonatomic) CGRect frame;
- (NSInteger)orientation;
- (id)window;
- (void)_prepareVisualProviderIfNeeded;
- (void)layoutSubviews;
@end
@implementation _UIStatusBar
- (NSInteger)orientation {return orientation;}
- (id)window {return [Window new];}
- (void)_prepareVisualProviderIfNeeded {++preparedBars;}
- (void)layoutSubviews {++laidOutBars;}
@end
static NSString *version(id,SEL) {return @"Version 16.7.16";}
static Constraint *constraint(id first,id second,NSInteger attr,CGFloat value) {
    Constraint *c=[Constraint new];c.firstItem=first;c.secondItem=second;c.firstAttribute=attr;c.secondAttribute=attr;c.multiplier=1;c.constant=value;c.active=YES;return c;
}
'''  + exact + '\n#include "HoujiStatusBarGeometry.h"\n' + r'''

int main(){@autoreleasepool {
 Method vm=class_getInstanceMethod(object_getClass(NSProcessInfo.processInfo),@selector(operatingSystemVersionString));
 IMP previous=method_setImplementation(vm,(IMP)version);
 houji_status_bar::install();method_setImplementation(vm,previous);
 assert(houji_status_bar::originalPrepare && houji_status_bar::originalLayout && houji_status_bar::originalData && houji_status_bar::originalAvoidance);
 _UIStatusBar *bar=[_UIStatusBar new];bar.frame=CGRectMake(0,0,600,50);
 auto p=[_UIStatusBarVisualProvider_DynamicSplit new];bar.visualProvider=p;p.statusBar=bar;
 auto container=[Container new];bar.containerView=container;container.frame=CGRectMake(0,0,400,54);
 auto guide=[Guide new];guide.owningView=container;guide.layoutFrame=CGRectMake(0,15,400,30);p.mainRegionsLayoutGuide=guide;
 auto left=[Region new],right=[Region new],other=[Region new];
 left.layoutItem=[NSObject new];right.layoutItem=[NSObject new];other.layoutItem=[NSObject new];
 bar.regions=@{@"left":left,@"right":right,@"expanded":other};
 auto lc=constraint(left.layoutItem,guide,10,0),rc=constraint(right.layoutItem,guide,10,2);
 auto lo=constraint(left.layoutItem,guide,5,17),ro=constraint(right.layoutItem,guide,6,-18);
 auto unrelated=constraint(other.layoutItem,guide,5,7);
 container.constraints=@[lc,rc,lo,ro,unrelated];p.leadingCenterYConstraint=lc;
 writes=0;
 [bar _prepareVisualProviderIfNeeded];[bar layoutSubviews];assert(writes==0); // Initial 600-point transient.
 bar.frame=CGRectMake(0,0,400,54);[bar layoutSubviews];
 assert(lc.constant==-4 && rc.constant==-2); // Preserve UIKit optical alignment offsets.
 assert(lo.constant==10 && ro.constant==-10 && unrelated.constant==7 && writes==4);
 for(unsigned i=0;i<40;++i){[bar _prepareVisualProviderIfNeeded];[bar layoutSubviews];}
 assert(preparedBars==41 && laidOutBars==42 && writes==4); // No repeated writes or recursion.
 expanded=true;[bar layoutSubviews];assert(lc.constant==0 && rc.constant==2 && lo.constant==17 && ro.constant==-18);
 expanded=false;[bar layoutSubviews];assert(lo.constant==10);
 orientation=3;[bar layoutSubviews];assert(lo.constant==17);orientation=1;[bar layoutSubviews];
 external=true;[bar layoutSubviews];assert(ro.constant==-18);external=false;[bar layoutSubviews];
 target=false;[bar layoutSubviews];assert(lc.constant==0);target=true;[bar layoutSubviews];
 // A UIKit update while adjusted becomes the new baseline, not an overwritten old value.
 lo.constant=19;[bar layoutSubviews];assert(lo.constant==10);
 guide.layoutFrame=CGRectMake(0,NAN,400,30);[bar layoutSubviews];assert(lo.constant==19);
 guide.layoutFrame=CGRectMake(0,15,400,30);[bar layoutSubviews];
 // Validate every candidate before any partial change; duplicates and inactive constraints fail closed.
 auto duplicate=constraint(right.layoutItem,guide,6,-18);
 container.constraints=@[lc,rc,lo,ro,unrelated,duplicate];[bar layoutSubviews];assert(lo.constant==19 && lc.constant==0);
 container.constraints=@[lc,rc,lo,ro,unrelated];rc.active=NO;[bar layoutSubviews];assert(lo.constant==19);rc.active=YES;
 [bar layoutSubviews];bar.visualProvider=[NSObject new];[bar layoutSubviews];assert(ro.constant==-18 && lo.constant==19);
 assert(unrelated.constant==7 && [objc_getAssociatedObject(bar,&houji_status_bar::savedConstraintsKey) count]==0);
 // UIKit's provider-update path can change geometry without laying out the bar.
 bar.visualProvider=p;[bar layoutSubviews];unsigned layouts=laidOutBars;
 expectedData=[NSObject new];[p dataUpdated:expectedData];assert(dataUpdates==1 && lc.constant==-2 && laidOutBars==layouts);
 expectedSettings=[NSObject new];previousFrame=CGRectMake(2,3,4,5);
 [p avoidanceFrameUpdatedFromFrame:previousFrame withAnimationSettings:expectedSettings options:0x13];
 assert(avoidanceUpdates==1 && lc.constant==-1 && laidOutBars==layouts);
 auto medium=[_UIStatusBarVisualProvider_DynamicSplitMedium new];medium.mainRegionsLayoutGuide=guide;medium.leadingCenterYConstraint=lc;medium.statusBar=bar;
 bar.visualProvider=medium;[bar layoutSubviews];assert(lc.constant==-1 && ro.constant==-10);
 expanded=true;[bar layoutSubviews];assert(lc.constant==3 && ro.constant==-18);expanded=false;
 auto saved=houji_status_bar::originalLayout;houji_status_bar::install();assert(saved==houji_status_bar::originalLayout);
 }puts("PASS: actual DynamicSplit hook, transient/final geometry, four existing constraints, bounded nonaccumulating displacement, provider update callbacks, subclass coverage, restoration and invalid topology; no iOS visual claim");}
'''
with tempfile.TemporaryDirectory(prefix='houji-statusbar-') as tmp:
    source,binary=Path(tmp)/'check.mm',Path(tmp)/'check';source.write_text(unit)
    subprocess.run(['xcrun','clang++','-std=c++17','-fobjc-arc','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-framework','Foundation','-framework','CoreGraphics','-I'+str(ROOT/'userland/display'),str(source),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
