<!-- MDTOC maxdepth:6 firsth1:1 numbering:0 flatten:0 bullets:1 updateOnSave:1 -->

- [Resources](#Resources)   
- [1.Description](#1Description)   
   - [1.1.GC Impact on Mobile devices](#11GC-Impact-on-Mobile-devices)   
   - [1.2.Proposed Solution](#12Proposed-Solution)   
      - [1.2.1.Conclusions](#121Conclusions)   
- [2.Experimental Environment](#2Experimental-Environment)   

<!-- /MDTOC -->
# Resources



* **One Process to Reap Them All: Garbage Collection as-a-Service**. Hussein, A., Payer, M., Hosking, A. L., and Vick, C. A. (2015). *In proceedings of the 13th ACM SIGPLAN/SIGOPS International Conference on Virtual Execution Environments (VEE’17). Xi’an, China*. doi:[10.1145/3050748.3050754](https://doi.org/10.1145/3140607.3050754).
* **Impact of GC Design on Power and Performance for Android**. Hussein, A., Payer, M., Hosking, A. L., and Vick, C. A. (2015).  *In ACM International Systems and Storage Conference*. doi:[10.1145/2757667.2757674](https://doi.org/10.1145/2757667.2757674).
* **Don’t Race the Memory Bus: Taming the GC Leadfoot**. Hussein, A., Hosking, A. L., Payer, M., and Vick, C. A. (2015).  *In ACM SIGPLAN International Symposium on Memory Management*. doi:[10.1145/2754169.2754182](https://doi.org/10.1145/2887746.2754182)


# 1.Description

Mobile devices are required to provide the desired performance and responsiveness while being constrained by energy consumption and thermal dissipation. With performance, heat, and power consumption strongly tied together it is common to use dynamic frequency at run-time to reduce power consumption and amount of generated heat (i.e., CPU throttling).

The dominance of Android’s runtime introduces an interesting challenge: we are faced with devices that continuously run dozens of managed language environments—also known as virtual machines (VMs)—in parallel. These VMs run both as “apps” in the foreground and as services in the background.

All concurrent VMs share a set of constrained and over-committed resources. Without global coordination, each VM optimizes independently across orthogonal goals: performance, responsiveness, and power consumption.
VM services such as garbage collection (GC) typically come with a number of optimization and scheduling heuristics designed to meet the performance needs of the supported applications and users. The tuning of GC performance is achieved by designing a GC policy that uses a set of predefined heuristics and the state of app execution to decide when and what to collect.

Configuring a garbage collector is a tedious task because a VM often uses tens of parameters when tuning the garbage collector, specific to the needs of a particular application: i.e., initial heap size, heap resizing, and the mode of collection to perform. Even for a single VM it is extremely difficult to identify the best collector and heuristics for all service configurations


## 1.1.GC Impact on Mobile devices

GC has a significant impact on energy consumed by the apps. This happens not only because of its explicit overhead on CPU and memory cycles, but also because of implicit scheduling decisions by the OS and hardware with respect to CPU cores. Therefore, a potential approach to optimize GC cost per single VM is to take advantage of GC idleness and control the frequency of the core on which the concurrent collector thread is running. Although this approach increases the responsiveness of applications and reduces memory consumption as perceived from a single VM, it is not feasible to achieve a global optimization criterion with multiple VMs.


## 1.2.Proposed Solution

We introduce a VM design that allows a central service to observe performance critical parameters of concurrent yet independent VMs and carry out decisions optimized across the whole system, instead of just locally. Our prototype then addresses a major resource bottleneck (i.e., memory) by presenting a central GC service that evaluates GC decisions across all running VMs and optimizes for a global set of heuristics. The new system aims at reducing the latency of app responses while assuring better performance and longer (battery) lifetimes. This is achieved without compromising the integrity of the platform.

### 1.2.1.Conclusions

The Android system is running dozens of concurrent VMs, each running an app on a single device in a constrained environment. Unfortunately, the mobile system so far treats each VM as a monolithic instance.

In our evaluation, we illustrate the impact of such a central memory management service in reducing total energy consumption (up to 18%) and increasing throughput (up to 12%), and improving memory utilization and adaptability to user activities.

The GC service has the following benefits:
1. it reduces the cost of GC by tuning GC scheduling decisions and coordinating with the power manager,
1. apps run in their own processes, ensuring separation between processes,
1. it eliminates sparse heaps, releasing more pages back to the system,
1. it performs opportunistic compaction and trimming on sparse heaps, reducing the total overhead needed to release memory from background apps,
1. it reduces the number of processes killed by the system LMK by returning more pages,
1. it saves device resources during memory recycling, and
1. it reduces the GC space overhead per VM—for example, instead of allocating internal data structures for each VM, heap structures are allocated by the global service.

# 2.Experimental Environment

Our centralized framework cuts across multiple layers of the Android 4.4.2 “KitKat” software stack and touches both hardware and operating system aspects. The default configuration appears in table1. We use the Monkeyrunner tool to automate user inputs.
We use the APQ8074 DragonBoard hardware Development Kit based on Qualcomm’s Snapdragon S4 SoC. The S4 uses the quad-core 2.3GHz Krait CPU. Caches are 4KiB + 4KiB direct mapped L0 cache, 16KiB + 16KiB 4-way set associative L1 cache, and 2MiB 8-way set associative L2 cache. The total memory available is 2GiB.


**Table1:** _Build properties in our experimental environment_
|                     |           |   |                        |           |
|:--------------------|:----------|:-:|-----------------------:|----------:|
| **VM parameter**    | **value** |   | **Governor parameter** | **value** |
| heapstartsize       | 8 MiB     |   |           optimal_freq |  0.96 GHz |
| heapgrowthlimit     | 96 MiB    |   |          sampling_rate |     50 ms |
| heapsize            | 256 MiB   |   |       scaling_max_freq |   2.1 GHz |
| heapmaxfree         | 8 MiB     |   |       scaling_min_freq |   0.3 GHz |
| heapminfree         | 2 MiB     |   |              sync_freq |  0.96 GHz |
| heaptargetutil      | 75 %      |   |     **lowmem_minfree** |    (page) |
| large obj threshold | 12 KiB    |   |    { 12288 15360 18432 |           |
| trim_threshold      | 75 %      |   |    21504 24576 30720 } |           |
