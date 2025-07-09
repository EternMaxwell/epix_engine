# Epix render module with vulkan backend

## Render app and Render schedule overview

The render app is a sub app added to main app by `RenderPlugin`.

There few system sets that are used to control the behavior of the render app.

Like in bevy but with some differences, we have the following system sets:

- `RenderSet::PrepareAssets`: This set is used to prepare assets that is extracted from the main app for rendering usage.
- `RenderSet::ManageViews`: This set is used to manage the views that are used for rendering. Views are basically cameras in gpu side that represents both physical cameras and logical cameras like those for shadow maps.
- `RenderSet::Queue`: Queue renderable entities and data as phase items and store each type of items in render phases.
- `RenderSet::PhaseSort`: Sort the renderable items in each phase. However, this can also be done in the `RenderSet::Queue` set.
- `RenderSet::Prepare`: Prepare the render resources from extracted data for gpu. Create descriptor sets that depends on them.
  - `RenderSet::PrepareResources`: sub set in `RenderSet::Prepare` that is used to prepare the render resources like textures, buffers, etc. And store the needed gpu commands that will needed to actually process them.
  - `RenderSet::PrepareFlush`: sub set in `RenderSet::Prepare` that is used to flush the command buffers.
  - `RenderSet::PrepareSets`: sub set in `RenderSet::Prepare` that is used to prepare the descriptor sets that will be used in the render pipeline.
- `RenderSet::Render`: This set does the actual rendering. The render is handled by a `RenderGraph`. All rendering and post processing is done in this set.
- `RenderSet::Cleanup`: This set is used to cleanup the render resources.

## Render phases

Render phases are used to group same type of renderable items together. And each item will has its own render pipeline and render commands. It is also possible to have multiple item batched together into one single item in `Queue` or `PhaseSort` set.

For each phase item in render phase, we will fetch the render commands from `DrawFunctions<[PhaseItem type]>` resource. Than call the `draw` function on that returned function to actually render the item. The pipeline will be fetched inside the `draw` function.

## Render graph

A render graph is a itself a directed acyclic graph (DAG) that describes the rendering process. It consists of nodes and edges, where nodes represent render passes and edges represent dependencies between them.

A graph can also contain subgraphs, which are still render graphs. In general, the first layer of the graph is the main render graph and does not actually do the rendering. It is used to manage different subgraphs. The `CameraPlugin` will add the only node to the main graph that triggers subgraph runs based on what type of cameras are used(e.g. 2d, 3d, etc.).

Subgraphs in a render graph is stored in a hash map with key being `GraphLabel` and value being the subgraph itself. The subgraph is also of type `RenderGraph`.

### Node

A node in render graph can be any type cause it is stored as pointer in render graph. As long as the type satisfies the `RenderNode` concept, e.g. has `inputs` and `outputs` methods that returns the input and output slot infos, and has a `update(World&)` and a `run(GraphContext&, RenderContext&, World&)` methods.

When running the whole render graph(the main), it will calls the graph's `update(World&)` function which will also update all its nodes and subgraphs. Then we will do run on the graph. Nodes when running in a graph is given the mutable access to `GraphContext` in which case it can trigger subgraph runs for this graph's subgraphs. The `RenderContext` is used to access render resources like device and commandbuffers, and the `World` is used to access any other resources that needed for the node and its render phases or phase items to run, including pipelines, buffers, textures that is not accessed in other nodes or subgraphs.

## Resources

Epix provides wrappers for common vulkan objects. `Buffer`, `Image`, `Sampler`, `ImageView`, `DescriptorSet`, `Shader`, `CommandBuffer`, `CommandPool`, etc. And provides the associated create info types(or descriptor types) for them and will be stored inside them. In this case, user is able to track the state of the resources.

## Pipelines

`Pipelines` are managed differently in epix. All pipelines will be stored in a `CachedPipelines` resource, and can be fetched using a `PipelineId`.

Suggested way to use this struct to create and get the pipeline you want is to use other two related objects, e.g. `SpecializedPipelines` struct and `SpecializedPipeline` template struct. A valid `SpecializedPipeline` requires it to have declared `Key` type and `Instructor` type and have a `specialize(const Key&, const Instructor&)` method that returns `PipelineDescriptor`. The `Key` type is used in SpecializedPipelines to determine whether it is created and has to be created, `Instructor` type is used for `SpecializedPipeline` to provide the necessary information for pipeline creation. Most of the time `Instructor` is not used since for a renderable object user should know all the details to create the pipeline. But this is useful for pipelines that is for similar but not equel renderable objects, for example `Mesh`es since they can have different attributes, therefore the vertex input attributes for pipeline is different. In this case, the `Instructor` can be used to provide the vertex input attributes for the pipeline creation. The `SpecializedPipelines` will have a `specialize(CachedPipelines&, const SpecializedPipeline<T>&, const GraphContext&, const Key&, const Instructor&)` method that will specialize the pipeline and store the created pipeline(if not already created) in the `CachedPipelines` resource and store and return the `PipelineId` of the created pipeline.

The `Key` and `Instructor` type must also implement `std::equal_to` and `std::hash` since they are used in hash map keys for `PipelineId` stored in `SpecializedPipelines`.