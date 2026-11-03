// SPDX-License-Identifier: MIT
// Included after the shared ABI checker and bounded geometry-only logger.
namespace houji_status_bar {
using InsetsMethod = Insets (*)(id, SEL);
unsigned reports, refusals;
constexpr CGFloat maximumUpwardShift = 4;
using BarMethod = void (*)(id, SEL);
BarMethod originalPrepare, originalLayout;
unsigned prepareReports, initialLayoutReports, layoutReports;
char observedStateKey;

bool getter(id receiver, const char *name, const char *type) {
    Method m = class_getInstanceMethod(object_getClass(receiver), sel_registerName(name));
    return m && method_getNumberOfArguments(m) == 2 && exactType(m, UINT_MAX, type);
}

void traceBar(id bar, const char *phase) {
    // Observe the provider selected by UIKit itself. A successful hook install
    // does not establish that FixedSplit is the active layout on this screen.
    if (!NSThread.isMainThread) return;
    @try {
        const auto object = [](id receiver, const char *name) {
            return getter(receiver, name, "@")
                ? ((id (*)(id, SEL))objc_msgSend)(receiver, sel_registerName(name)) : nil;
        };
        id provider = object(bar, "visualProvider");
        CGRect frame = {};
        if (getter(bar, "frame", @encode(CGRect)))
            frame = ((CGRect (*)(id, SEL))objc_msgSend)(bar, sel_registerName("frame"));
        id window = object(bar, "window");
        id screen = object(window, "screen");
        CGRect native = {};
        if (getter(screen, "nativeBounds", @encode(CGRect)))
            native = ((CGRect (*)(id, SEL))objc_msgSend)(screen, sel_registerName("nativeBounds"));
        const NSInteger orientation = getter(bar, "orientation", @encode(NSInteger))
            ? ((NSInteger (*)(id, SEL))objc_msgSend)(bar, sel_registerName("orientation")) : -1;
        const BOOL expanded = getter(provider, "expanded", @encode(BOOL))
            ? ((BOOL (*)(id, SEL))objc_msgSend)(provider, sel_registerName("expanded")) : NO;
        NSString *state = [NSString stringWithFormat:@"bar=%@ provider=%@ orientation=%ld expanded=%d window=%d frame=%.2f,%.2f,%.2f,%.2f native=%.0fx%.0f",
            NSStringFromClass(object_getClass(bar)), NSStringFromClass(object_getClass(provider)),
            (long)orientation, expanded, window != nil, frame.origin.x, frame.origin.y, frame.size.width,
            frame.size.height, native.size.width, native.size.height];
        if ([state isEqual:objc_getAssociatedObject(bar, &observedStateKey)] || layoutReports >= 64) return;
        objc_setAssociatedObject(bar, &observedStateKey, state, OBJC_ASSOCIATION_COPY_NONATOMIC);
        ++layoutReports;
        report(@"HOUJI STATUSBAR v5: %s %@", phase, state);
        for (const char *name : {"leadingEdgeInsets", "trailingEdgeInsets", "edgeInsets"}) {
            if (!getter(provider, name, "{NSDirectionalEdgeInsets=dddd}")) continue;
            const Insets value = ((InsetsMethod)objc_msgSend)(provider, sel_registerName(name));
            report(@"HOUJI STATUSBAR v5: %s=%.3f,%.3f,%.3f,%.3f", name,
                value.top, value.left, value.bottom, value.right);
        }
    } @catch (NSException *exception) {
        report(@"HOUJI STATUSBAR v5: observation stopped after %@", exception.name);
    }
}

void prepareBar(id bar, SEL selector) {
    originalPrepare(bar, selector);
    if (NSThread.isMainThread && prepareReports++ < 8) traceBar(bar, "prepare");
}

void adjustDynamic(id bar);

void layoutBar(id bar, SEL selector) {
    originalLayout(bar, selector);
    adjustDynamic(bar);
    if (NSThread.isMainThread && initialLayoutReports++ < 12) traceBar(bar, "layout");
}

void installObservation() {
    if (originalPrepare || originalLayout) return;
    Class bar = NSClassFromString(@"_UIStatusBar");
    SEL prepare = sel_registerName("_prepareVisualProviderIfNeeded");
    SEL layout = sel_registerName("layoutSubviews");
    Method pm = class_getInstanceMethod(bar, prepare), lm = class_getInstanceMethod(bar, layout);
    for (Method m : {pm, lm}) {
        if (!m || method_getNumberOfArguments(m) != 2 || !exactType(m, UINT_MAX, "v")) {
            report(@"HOUJI STATUSBAR v5: observation ABI refused");
            return;
        }
    }
    originalPrepare = (BarMethod)method_getImplementation(pm);
    originalLayout = (BarMethod)method_getImplementation(lm);
    class_replaceMethod(bar, prepare, (IMP)prepareBar, method_getTypeEncoding(pm));
    class_replaceMethod(bar, layout, (IMP)layoutBar, method_getTypeEncoding(lm));
    report(@"HOUJI STATUSBAR v5: provider observation installed");
}

// NSLayoutAttribute values from UIKit. All messages below are checked against
// the target Objective-C ABI; no UIKit class or symbol is linked into the shim.
constexpr NSInteger leadingAttribute = 5, trailingAttribute = 6, centreAttribute = 10;
char savedConstraintsKey, savedConstantKey;

id object(id receiver, const char *name) {
    return getter(receiver, name, "@")
        ? ((id (*)(id, SEL))objc_msgSend)(receiver, sel_registerName(name)) : nil;
}

template<class T> bool read(id receiver, const char *name, T &value) {
    if (!getter(receiver, name, @encode(T))) return false;
    value = ((T (*)(id, SEL))objc_msgSend)(receiver, sel_registerName(name));
    return true;
}

bool canSet(id receiver, const char *name, const char *argument) {
    Method m = class_getInstanceMethod(object_getClass(receiver), sel_registerName(name));
    return m && method_getNumberOfArguments(m) == 3 && exactType(m, UINT_MAX, "v") &&
        exactType(m, 2, argument);
}

void setConstant(id constraint, CGFloat value) {
    ((void (*)(id, SEL, CGFloat))objc_msgSend)(constraint, sel_registerName("setConstant:"), value);
}

// Keep weak references so a discarded UIKit provider and its views can die.
// Restore only our last value: an independent UIKit update takes precedence.
void restore(id bar, NSArray *keeping) {
    NSHashTable *saved = objc_getAssociatedObject(bar, &savedConstraintsKey);
    for (id constraint in saved.allObjects) {
        if ([keeping containsObject:constraint]) continue;
        NSArray *values = objc_getAssociatedObject(constraint, &savedConstantKey);
        CGFloat value;
        if (values.count == 2 && read(constraint, "constant", value) &&
            value == [values[1] doubleValue] && canSet(constraint, "setConstant:", @encode(CGFloat)))
            setConstant(constraint, [values[0] doubleValue]);
        objc_setAssociatedObject(constraint, &savedConstantKey, nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        [saved removeObject:constraint];
    }
}

bool matches(id c, id first, id second, NSInteger attribute) {
    NSInteger a, b, relation;
    CGFloat multiplier, constant;
    BOOL active;
    return object(c, "firstItem") == first && object(c, "secondItem") == second &&
        read(c, "firstAttribute", a) && a == attribute &&
        read(c, "secondAttribute", b) && b == attribute &&
        read(c, "relation", relation) && relation == 0 &&
        read(c, "multiplier", multiplier) && multiplier == 1 &&
        read(c, "constant", constant) && std::isfinite(constant) && std::abs(constant) <= 60 &&
        read(c, "isActive", active) && active && canSet(c, "setConstant:", @encode(CGFloat));
}

id uniqueConstraint(NSArray *constraints, id first, id second, NSInteger attribute) {
    id found = nil;
    for (id c in constraints) {
        if (!matches(c, first, second, attribute)) continue;
        if (found) return nil;
        found = c;
    }
    return found;
}

CGFloat originalConstant(id constraint) {
    CGFloat current = 0;
    read(constraint, "constant", current);
    NSArray *previous = objc_getAssociatedObject(constraint, &savedConstantKey);
    return previous.count == 2 && current == [previous[1] doubleValue]
        ? [previous[0] doubleValue] : current;
}

bool plan(id bar, NSArray **constraints, NSArray **constants, unsigned &stage) {
    stage = 0;
    id provider = object(bar, "visualProvider");
    if (![provider isKindOfClass:NSClassFromString(@"_UIStatusBarVisualProvider_DynamicSplit")]) return false;
    NSInteger orientation;
    BOOL expanded;
    CGRect frame, native, bounds, guideFrame, containerFrame;
    CGFloat scale;
    id main = object(NSClassFromString(@"UIScreen"), "mainScreen");
    id screen = object(object(bar, "window"), "screen");
    if (!screen || screen != main || !read(bar, "orientation", orientation) || orientation != 1 ||
        !read(provider, "expanded", expanded) || expanded || !read(bar, "frame", frame) ||
        !read(screen, "nativeBounds", native) || !read(screen, "bounds", bounds) || !read(screen, "scale", scale) ||
        native.size.width != 1200 || native.size.height != 2670 ||
        bounds.size.width != 400 || bounds.size.height != 890 || scale != 3 ||
        frame.origin.x != 0 || frame.origin.y != 0 || frame.size.width != 400 || frame.size.height != 54) return false;
    // r298 observes a temporary 600-point bar before the final 400-point layout.
    // Changing edgeInsets during setup would bake the wrong geometry into UIKit.
    stage = 1;
    id guide = object(provider, "mainRegionsLayoutGuide");
    id container = object(bar, "containerView");
    if (!guide || !container || object(guide, "owningView") != container ||
        !read(container, "frame", containerFrame) || containerFrame.origin.x != 0 || containerFrame.origin.y != 0 ||
        !read(guide, "layoutFrame", guideFrame) || guideFrame.size.width != 400 ||
        !std::isfinite(guideFrame.origin.y) || !std::isfinite(guideFrame.size.height) ||
        guideFrame.size.height < 12 || guideFrame.size.height > 54 ||
        guideFrame.origin.y < 0 || (guideFrame.origin.y + guideFrame.size.height) > 54) return false;
    stage = 2;
    NSArray *all = object(container, "constraints");
    NSDictionary *regions = object(bar, "regions");
    if (![all isKindOfClass:NSArray.class] || ![regions isKindOfClass:NSDictionary.class]) return false;
    stage = 3;
    id leadingCentre = object(provider, "leadingCenterYConstraint");
    id leadingItem = object(leadingCentre, "firstItem");
    if (!leadingItem || !matches(leadingCentre, leadingItem, guide, centreAttribute) ||
        ![all containsObject:leadingCentre]) return false;
    stage = 4;
    id leadingOuter = uniqueConstraint(all, leadingItem, guide, leadingAttribute);
    id trailingCentre = nil, trailingOuter = nil;
    bool leadingIsRegion = false;
    // Match the normal regions by their constraint topology, not private string
    // identifiers or a UIView assumption (a region may expose a UILayoutGuide).
    for (id region in regions.allValues) {
        id item = object(region, "layoutItem");
        if (!item) continue;
        if (item == leadingItem) { leadingIsRegion = true; continue; }
        id outer = uniqueConstraint(all, item, guide, trailingAttribute);
        id centre = uniqueConstraint(all, item, guide, centreAttribute);
        if (outer && centre) {
            if (trailingOuter) return false;
            trailingOuter = outer; trailingCentre = centre;
        }
    }
    stage = 5;
    CGFloat cap;
    if (!leadingIsRegion || !leadingOuter || !trailingOuter ||
        !read(object(provider, "normalFont"), "capHeight", cap) || !std::isfinite(cap) || cap < 6 || cap > 25) return false;
    // Android's board overlay defines a 124-pixel status-bar band. This is a
    // layout reference, not a measurement of the camera's physical centre.
    const CGFloat centre = 124.0 / (2 * scale);
    const CGFloat leadingBase = originalConstant(leadingCentre), trailingBase = originalConstant(trailingCentre);
    const CGFloat guideCentre = guideFrame.origin.y + guideFrame.size.height / 2;
    // r299's absolute centre correction moved SpringBoard too high. Preserve
    // UIKit's relative clock/icon offsets and limit their shared displacement.
    // Always use the saved baseline, so repeated layouts cannot accumulate it.
    const CGFloat displacement = std::clamp(centre - guideCentre - leadingBase, -maximumUpwardShift, CGFloat(0));
    for (CGFloat baseline : {leadingBase, trailingBase}) {
        const CGFloat y = guideCentre + baseline + displacement;
        if (y - (cap + 1) / 2 < 4 || y + (cap + 1) / 2 > frame.size.height) return false;
    }
    *constraints = @[leadingCentre, trailingCentre, leadingOuter, trailingOuter];
    *constants = @[@(leadingBase + displacement), @(trailingBase + displacement), @10, @-10];
    return true;
}

void adjustDynamic(id bar) {
    if (!NSThread.isMainThread) return;
    @try {
        NSArray *constraints = nil, *constants = nil;
        unsigned stage = 0;
        if (!plan(bar, &constraints, &constants, stage)) {
            restore(bar, nil);
            if (stage && refusals++ < 8) {
                CGRect guideFrame = {}, containerFrame = {};
                id provider = object(bar, "visualProvider"), container = object(bar, "containerView");
                read(object(provider, "mainRegionsLayoutGuide"), "layoutFrame", guideFrame);
                read(container, "frame", containerFrame);
                report(@"HOUJI STATUSBAR v5: topology deferred stage=%u guide=%.2f,%.2f,%.2f,%.2f container=%.2f,%.2f,%.2f,%.2f",
                    stage, guideFrame.origin.x, guideFrame.origin.y, guideFrame.size.width, guideFrame.size.height,
                    containerFrame.origin.x, containerFrame.origin.y, containerFrame.size.width, containerFrame.size.height);
            }
            return;
        }
        restore(bar, constraints);
        NSHashTable *saved = objc_getAssociatedObject(bar, &savedConstraintsKey);
        if (!saved) {
            saved = [NSHashTable weakObjectsHashTable];
            objc_setAssociatedObject(bar, &savedConstraintsKey, saved, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        }
        bool changed = false;
        for (NSUInteger i = 0; i < constraints.count; ++i) {
            id c = constraints[i];
            CGFloat current = 0; read(c, "constant", current);
            const CGFloat target = [constants[i] doubleValue];
            NSArray *previous = objc_getAssociatedObject(c, &savedConstantKey);
            const CGFloat baseline = previous.count == 2 && current == [previous[1] doubleValue]
                ? [previous[0] doubleValue] : current;
            objc_setAssociatedObject(c, &savedConstantKey, @[@(baseline), @(target)], OBJC_ASSOCIATION_RETAIN_NONATOMIC);
            [saved addObject:c];
            if (std::abs(current - target) > 0.001) { setConstant(c, target); changed = true; }
        }
        // Setting a constraint schedules UIKit's next layout. No recursive
        // layoutIfNeeded, transforms, new constraints or display-size changes.
        if (changed && reports++ < 12) report(@"HOUJI STATUSBAR v5: DynamicSplit applied maximumUpwardShift=%.3f outer=10,10 offsets=%@", maximumUpwardShift, constants);
    } @catch (NSException *exception) {
        restore(bar, nil);
        if (reports++ < 12) report(@"HOUJI STATUSBAR v5: geometry retained after %@", exception.name);
    }
}

using DataMethod = void (*)(id, SEL, id);
using AvoidanceMethod = void (*)(id, SEL, CGRect, id, NSUInteger);
DataMethod originalData;
AvoidanceMethod originalAvoidance;

void afterProviderUpdate(id provider, const char *phase) {
    id bar = object(provider, "statusBar");
    if (!bar) return;
    adjustDynamic(bar);
    if (NSThread.isMainThread && layoutReports < 64) traceBar(bar, phase);
}

void dataUpdated(id provider, SEL selector, id data) {
    originalData(provider, selector, data);
    afterProviderUpdate(provider, "data-update");
}

void avoidanceUpdated(id provider, SEL selector, CGRect previous, id settings, NSUInteger options) {
    originalAvoidance(provider, selector, previous, settings, options);
    afterProviderUpdate(provider, "avoidance-update");
}

void installProviderUpdates() {
    if (originalData || originalAvoidance) return;
    Class cls = NSClassFromString(@"_UIStatusBarVisualProvider_DynamicSplit");
    SEL data = sel_registerName("dataUpdated:");
    SEL avoidance = sel_registerName("avoidanceFrameUpdatedFromFrame:withAnimationSettings:options:");
    Method dm = class_getInstanceMethod(cls, data), am = class_getInstanceMethod(cls, avoidance);
    if (!dm || !am || method_getNumberOfArguments(dm) != 3 || method_getNumberOfArguments(am) != 5 ||
        !exactType(dm, UINT_MAX, "v") || !exactType(dm, 2, "@") || !exactType(am, UINT_MAX, "v") ||
        !exactType(am, 2, @encode(CGRect)) || !exactType(am, 3, "@") || !exactType(am, 4, @encode(NSUInteger))) {
        report(@"HOUJI STATUSBAR v5: provider update ABI refused");
        return;
    }
    // Target UIKit can lay out the guide's owning container directly during an
    // avoidance update, bypassing _UIStatusBar.layoutSubviews. Reapply after
    // that original path as well as ordinary bar layout; preserve all arguments.
    originalData = (DataMethod)method_getImplementation(dm);
    originalAvoidance = (AvoidanceMethod)method_getImplementation(am);
    class_replaceMethod(cls, data, (IMP)dataUpdated, method_getTypeEncoding(dm));
    class_replaceMethod(cls, avoidance, (IMP)avoidanceUpdated, method_getTypeEncoding(am));
    report(@"HOUJI STATUSBAR v5: provider update hooks installed");
}

void install() {
    if (![NSProcessInfo.processInfo.operatingSystemVersionString containsString:@"16.7.16"]) return;
    installObservation();
    installProviderUpdates();
}
}
